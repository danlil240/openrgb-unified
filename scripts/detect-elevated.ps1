# Runs the baseline OpenRGB build elevated and captures detection results.
# Optionally stops FanControl first (it holds the single-client PawnIO driver)
# and relaunches it afterwards via its scheduled task.
#
# Usage (from an elevated PowerShell, or via Start-Process -Verb RunAs):
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\detect-elevated.ps1 [-StopFanControl] [-ExtraArgs "-ld"]
param(
    [switch]$StopFanControl,
    [string]$ExtraArgs = "-ld"
)

$root   = Split-Path -Parent $PSScriptRoot
$exe    = Join-Path $root "OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"
$work   = Join-Path $root "build-test\elevated"

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script must run elevated (PawnIO/SMBus access)."
    exit 1
}

$fanCtlWasRunning = $false
if ($StopFanControl) {
    $fanCtlWasRunning = [bool](Get-Process -Name FanControl -ErrorAction SilentlyContinue)
    Get-Process -Name FanControl, "FanControl.Service" -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 3
}

New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
try {
    & $exe --localconfig --noautoconnect --verbose $ExtraArgs.Split(' ') | Out-Null
} finally {
    Pop-Location
}

if ($StopFanControl -and $fanCtlWasRunning) {
    # FanControl is normally launched elevated by its scheduled task
    try { Start-ScheduledTask -TaskName "FanControl" -ErrorAction Stop }
    catch {
        $fanCtlExe = "C:\Program Files (x86)\FanControl\FanControl.exe"
        if (Test-Path $fanCtlExe) { Start-Process $fanCtlExe }
    }
}

$latest = Get-ChildItem (Join-Path $work "logs") -Filter *.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Write-Host "Log: $($latest.FullName)"
