# Runs the INSTALLED OpenRGB build elevated (FanControl stopped first) to
# check whether Corsair DIMM detection works there. Results in
# build-test\elevated-installed\logs\
$work   = "C:\Users\daniel\tools\openrgb-unified\build-test\elevated-installed"
$exe    = "C:\Program Files\OpenRGB\OpenRGB.exe"
$out    = Join-Path $work "installed-run.txt"

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "must run elevated"; exit 1
}

Get-Process -Name OpenRGB, FanControl, "FanControl.Service" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
try { & $exe --localconfig --noautoconnect --verbose -ld | Out-Null } finally { Pop-Location }

$latest = Get-ChildItem (Join-Path $work "logs") -Filter *.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
"Log: $($latest.FullName)" | Out-File $out
(Select-String -Path $latest.FullName -Pattern "PawnIO|Registering RGB controller|Corsair|DRAM|slot|SPD" | ForEach-Object { $_.Line.Trim() }) | Out-File $out -Append

try { Start-ScheduledTask -TaskName "FanControl" -ErrorAction Stop } catch { Start-Process "C:\Program Files (x86)\FanControl\FanControl.exe" }
