# Starts our baseline OpenRGB build elevated as an SDK server (detached).
# Config/logs go to build-test\server\ via --localconfig.
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build-test\server"
$exe  = Join-Path $root "OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "must run elevated"; exit 1
}

Get-Process -Name OpenRGB -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
New-Item -ItemType Directory -Force $work | Out-Null

# Start detached, elevated, hidden window, cwd = $work (localconfig)
Start-Process -FilePath $exe -WorkingDirectory $work -WindowStyle Hidden `
    -ArgumentList "--localconfig --noautoconnect --server --verbose"
