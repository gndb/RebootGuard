param()
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $root 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid VERSION' }
$exe = Join-Path $root 'build\RebootGuard.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Run build.ps1 before packaging.' }
$dist = Join-Path $root 'dist'
$name = "RebootGuard-$version-windows-x64"
$stage = Join-Path $dist $name
$archive = Join-Path $dist "$name.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) { throw 'This version is already packaged. Keep the existing package or choose a new VERSION.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($file in @($exe, (Join-Path $root 'install.ps1'), (Join-Path $root 'README.md'), (Join-Path $root 'VERSION'))) {
    Copy-Item -LiteralPath $file -Destination $stage
}
foreach ($folder in @('assets','docs')) { New-Item -ItemType Directory -Path (Join-Path $stage $folder) | Out-Null }
Copy-Item -LiteralPath (Join-Path $root 'assets\preview.png') -Destination (Join-Path $stage 'assets')
Copy-Item -LiteralPath (Join-Path $root 'docs\IMPLEMENTATION.md') -Destination (Join-Path $stage 'docs')
$hash = (Get-FileHash -LiteralPath (Join-Path $stage 'RebootGuard.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $stage 'SHA256SUMS.txt'), "$hash  RebootGuard.exe`n", [Text.Encoding]::ASCII)
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $dist 'SHA256SUMS.txt'), "$zipHash  $name.zip`n", [Text.Encoding]::ASCII)
Get-Item -LiteralPath $archive | Select-Object Name,Length
