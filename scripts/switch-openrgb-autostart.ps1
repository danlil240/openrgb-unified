# switch-openrgb-autostart.ps1
# Run ELEVATED. Repoints the "openRGB" scheduled task (which launches
# OpenRGB elevated at logon, silently — RunLevel Highest) at the locally
# built wireless-enabled OpenRGB, stops any running OpenRGB instances,
# disables the non-elevated Startup-folder shortcut, and starts the task.
#
# Revert: point the action back to "C:\Program Files\OpenRGB\OpenRGB.exe"
# and re-enable the renamed Startup shortcut.

$ErrorActionPreference = 'Stop'

$buildDir  = 'C:\Users\daniel\tools\openrgb-unified\OpenRGB\OpenRGB Windows 64-bit'
$exe       = Join-Path $buildDir 'OpenRGB.exe'
$taskName  = 'openRGB'
$startupLnk = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\OpenRGB.lnk"

if (-not (Test-Path $exe)) { throw "Build output not found: $exe" }

# Repoint + enable the elevated logon task
$action = New-ScheduledTaskAction -Execute $exe -Argument '--startminimized' -WorkingDirectory $buildDir
Set-ScheduledTask -TaskName $taskName -Action $action | Out-Null
Enable-ScheduledTask -TaskName $taskName | Out-Null
Write-Host "Task '$taskName' -> $exe (enabled)"

# The Startup-folder shortcut would launch a second non-elevated
# instance that fights the elevated one over the SDK port.
if (Test-Path $startupLnk) {
    Rename-Item $startupLnk 'OpenRGB.lnk.disabled'
    Write-Host 'Startup shortcut disabled (renamed to OpenRGB.lnk.disabled)'
}

# Stop running instances, then start the elevated one now
Get-Process OpenRGB -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
Start-ScheduledTask -TaskName $taskName
Write-Host 'Started OpenRGB via scheduled task'
