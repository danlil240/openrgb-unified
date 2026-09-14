# disable-lconnect.ps1
# Run ELEVATED. Disables all L-Connect autostart mechanisms now that
# OpenRGB (wireless-enabled build) owns the Lian Li fan RGB:
#
#   - LConnectService          (Automatic -> Disabled, stopped)
#   - LConnectServiceWatcher   (Automatic -> Disabled, stopped)
#   - Scheduled task "L-Connect 3" (logon GUI launch -> Disabled)
#
# Nothing is uninstalled. Revert by re-running with -Enable instead:
#   powershell -ExecutionPolicy Bypass -File disable-lconnect.ps1 -Enable

param([switch]$Enable)

$ErrorActionPreference = 'Stop'

$services = 'LConnectService', 'LConnectServiceWatcher'
$taskName = 'L-Connect 3'

foreach ($svc in $services) {
    if (-not (Get-Service $svc -ErrorAction SilentlyContinue)) {
        Write-Host "$svc not present, skipping"
        continue
    }
    if ($Enable) {
        Set-Service $svc -StartupType Automatic
        Write-Host "$svc -> Automatic"
    } else {
        Stop-Service $svc -Force -ErrorAction SilentlyContinue
        Set-Service $svc -StartupType Disabled
        Write-Host "$svc -> Disabled"
    }
}

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
    if ($Enable) {
        Enable-ScheduledTask -TaskName $taskName | Out-Null
        Write-Host "Task '$taskName' enabled"
    } else {
        Disable-ScheduledTask -TaskName $taskName | Out-Null
        Write-Host "Task '$taskName' disabled"
    }
} else {
    Write-Host "Task '$taskName' not present, skipping"
}

if (-not $Enable) {
    Get-Process | Where-Object Name -match 'L.?Connect' | Stop-Process -Force
    Write-Host 'Done — L-Connect will not start at next logon.'
}
