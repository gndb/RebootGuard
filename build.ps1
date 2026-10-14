param([switch]$TestsOnly, [switch]$NativeTests)
$ErrorActionPreference = 'Stop'
if ($TestsOnly -and $NativeTests) { throw 'Choose -TestsOnly or -NativeTests, not both.' }
$root = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC x64 tools were not found. Install the Desktop development with C++ workload.' }
$vcVersion = (Get-Content -LiteralPath (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$vc = Join-Path $vs "VC\Tools\MSVC\$vcVersion"
$sdk = $null
foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')) {
    $kits = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    if ($kits.KitsRoot10 -and (Test-Path -LiteralPath (Join-Path $kits.KitsRoot10 'Include'))) { $sdk = $kits.KitsRoot10; break }
}
if (!$sdk) { $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' }
$version = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h')) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\stdio.h')) -and
        (Test-Path -LiteralPath (Join-Path $sdk "Lib\$($_.Name)\um\x64\user32.lib")) -and
        (Test-Path -LiteralPath (Join-Path $sdk "Lib\$($_.Name)\ucrt\x64\libucrt.lib")) -and
        (Test-Path -LiteralPath (Join-Path $sdk "bin\$($_.Name)\x64\rc.exe"))
    } | Select-Object -First 1 -ExpandProperty Name
if (!$version) { throw 'A complete Windows SDK with x64 libraries and rc.exe was not found.' }
$cl = Join-Path $vc 'bin\Hostx64\x64\cl.exe'
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$common = @('/nologo','/std:c++17','/utf-8','/W4','/WX','/O1','/MT','/EHsc',
    "/I$root\src", "/I$vc\include", "/I$sdk\Include\$version\ucrt", "/I$sdk\Include\$version\shared", "/I$sdk\Include\$version\um")
$libs = @("/LIBPATH:$vc\lib\x64", "/LIBPATH:$sdk\Lib\$version\ucrt\x64", "/LIBPATH:$sdk\Lib\$version\um\x64", '/OPT:REF','/OPT:ICF','/DYNAMICBASE','/NXCOMPAT')
Write-Host "Building x64 with MSVC $vcVersion and Windows SDK $version"
& $cl @common (Join-Path $root 'tests\tests.cpp') "/Fo$out\tests.obj" "/Fe$out\tests.exe" /link @libs
if ($LASTEXITCODE -ne 0) { throw 'Tests compilation failed' }
& (Join-Path $out 'tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Guard lifecycle checks failed' }
if ($TestsOnly) { return }
Push-Location $root
try {
    & "$sdk\bin\$version\x64\rc.exe" /nologo "/fo$out\resources.res" 'src\resources.rc'
    if ($LASTEXITCODE -ne 0) { throw 'Icon resources compilation failed' }
} finally { Pop-Location }
& $cl @common /DUNICODE /D_UNICODE (Join-Path $root 'src\RebootGuard.cpp') "/Fo$out\RebootGuard.obj" "/Fe$out\RebootGuard.exe" /link @libs /SUBSYSTEM:WINDOWS (Join-Path $out 'resources.res') user32.lib shell32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed' }
Get-Item -LiteralPath (Join-Path $out 'RebootGuard.exe') | Select-Object Name,Length
if ($NativeTests) {
    # Isolated instance and a stubbed cancellation call; never initiates shutdown.
    & $cl @common /DUNICODE /D_UNICODE /DGUARD_TEST_INSTANCE (Join-Path $root 'src\RebootGuard.cpp') "/Fo$out\RebootGuard-test.obj" "/Fe$out\RebootGuard-test.exe" /link @libs /SUBSYSTEM:WINDOWS (Join-Path $out 'resources.res') user32.lib shell32.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Isolated application compilation failed' }
    & $cl @common /DUNICODE /D_UNICODE (Join-Path $root 'tests\native-tests.cpp') "/Fo$out\native-tests.obj" "/Fe$out\native-tests.exe" /link @libs user32.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Native checks compilation failed' }
    & (Join-Path $out 'native-tests.exe') (Join-Path $out 'RebootGuard-test.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Native lifecycle checks failed' }
}
