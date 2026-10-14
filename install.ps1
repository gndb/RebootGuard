[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference = 'Stop'
$built = Join-Path $PSScriptRoot 'RebootGuard.exe'
if (!(Test-Path -LiteralPath $built)) { $built = Join-Path $PSScriptRoot 'build\RebootGuard.exe' }
if (!(Test-Path -LiteralPath $built)) { throw 'Build the application first, or extract the release ZIP before installing.' }
$appDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'RebootGuard'
$installed = Join-Path $appDir 'RebootGuard.exe'
$desktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) '重启保护.lnk'
$startupLink = Join-Path ([Environment]::GetFolderPath('Startup')) '重启保护.lnk'
foreach ($target in @($installed, $desktopLink, $startupLink)) {
    if (Test-Path -LiteralPath $target) { throw "Refusing to overwrite existing file: $target. See README for manual updates." }
}
if (!$PSCmdlet.ShouldProcess($appDir, 'Install RebootGuard, create current-user desktop/startup shortcuts, and launch protection')) { return }
New-Item -ItemType Directory -Path $appDir -Force | Out-Null
Copy-Item -LiteralPath $built -Destination $installed
if ((Get-FileHash -LiteralPath $built).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed EXE hash mismatch' }
$shell = New-Object -ComObject WScript.Shell
foreach ($linkPath in @($desktopLink, $startupLink)) {
    $link = $shell.CreateShortcut($linkPath)
    $link.TargetPath = $installed
    $link.WorkingDirectory = $appDir
    $link.IconLocation = "$installed,0"
    $link.Description = '重启保护：托盘菜单可解除保护；不会主动重启电脑'
    $link.WindowStyle = 7
    $link.Save()
    if ($shell.CreateShortcut($linkPath).TargetPath -ne $installed) { throw "Shortcut verification failed: $linkPath" }
}
Start-Process -FilePath $installed -WindowStyle Hidden
Write-Host "Installed: $installed"
Write-Host 'Windows Update and power policies were not changed. Check the tray icon for protection status.'
