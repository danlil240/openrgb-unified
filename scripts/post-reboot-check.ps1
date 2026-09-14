# Post-reboot baseline check.
# Stops every known RGB-owning app/service, then starts our baseline
# OpenRGB build elevated as an SDK server (port 6742, localconfig in
# build-test\post-reboot\). FanControl is intentionally left alone.
# Run elevated: powershell -ExecutionPolicy Bypass -File post-reboot-check.ps1

$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build-test\post-reboot"
$exe  = Join-Path $root "OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"
$out  = Join-Path $work "post-reboot-check.txt"

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "must run elevated"; exit 1
}

New-Item -ItemType Directory -Force $work | Out-Null
"" | Out-File $out

$svcs = "LConnectService","LConnectServiceWatcher","GigabyteUpdateService","AorusLcdService","logi_lamparray_service","SteelSeriesGGUpdateServiceProxy"
foreach ($s in $svcs) {
    $svc = Get-Service -Name $s -ErrorAction SilentlyContinue
    if ($svc) {
        "service $s : $($svc.Status) -> stop" | Out-File $out -Append
        Stop-Service -Name $s -Force -ErrorAction SilentlyContinue
    }
}

Get-Process | Where-Object { $_.ProcessName -match "Razer|Synapse|SteelSeries|Gigabyte|GCC|Aorus|LConnect|logi|G HUB|lghub" } | ForEach-Object {
    "kill $($_.ProcessName) $($_.Id)" | Out-File $out -Append
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Seconds 1

Get-Process -Name OpenRGB -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

Start-Process -FilePath $exe -WorkingDirectory $work -WindowStyle Hidden `
    -ArgumentList "--localconfig --noautoconnect --server --verbose"
"server launched -> $work" | Out-File $out -Append
"done" | Out-File $out -Append
