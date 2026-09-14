# Combined PawnIO diagnostic + cleanup — runs elevated, ONE UAC prompt.
# 1. Kills all stale OpenRGB processes (they may hold the single-client PawnIO driver)
# 2. Stops FanControl
# 3. Tests opening \\.\PawnIO directly and reports the exact error
# 4. Runs our baseline OpenRGB build detection with --localconfig
# 5. Restarts FanControl via its scheduled task
# Results go to build-test\elevated\pawnio-diag.txt and logs\

$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build-test\elevated"
$out  = Join-Path $work "pawnio-diag.txt"
$exe  = Join-Path $root "OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"

$lines = @()
$lines += "== PawnIO diag $(Get-Date -Format o) =="

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "must run elevated"; exit 1
}

$lines += "`n-- kill stale OpenRGB --"
Get-Process -Name OpenRGB -ErrorAction SilentlyContinue | ForEach-Object {
    try { Stop-Process -Id $_.Id -Force -ErrorAction Stop; $lines += "killed $($_.Id)" }
    catch { $lines += "kill FAIL $($_.Id): $($_.Exception.Message)" }
}

$lines += "`n-- stop FanControl --"
Get-Process -Name FanControl, "FanControl.Service" -ErrorAction SilentlyContinue | ForEach-Object {
    try { Stop-Process -Id $_.Id -Force; $lines += "stopped $($_.ProcessName) $($_.Id)" } catch { $lines += "stop FAIL $($_.Id)" }
}
Start-Sleep -Seconds 3

$lines += "`n-- sc query PawnIO --"
$lines += (sc.exe query PawnIO 2>&1 | Out-String).Trim()

$lines += "`n-- direct device open test --"
foreach ($path in @("\\.\PawnIO", "\\?\GLOBALROOT\Device\PawnIO")) {
    try {
        $fs = [System.IO.File]::Open($path, 'Open', 'ReadWrite', 'ReadWrite')
        $lines += "OPEN OK: $path"
        $fs.Close()
    } catch {
        $lines += "OPEN FAIL: $path -> $($_.Exception.Message)"
    }
}

$lines += "`n-- run baseline OpenRGB -ld --"
New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
try { & $exe --localconfig --noautoconnect --verbose -ld | Out-Null } finally { Pop-Location }
$latest = Get-ChildItem (Join-Path $work "logs") -Filter *.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$lines += "log: $($latest.FullName)"
$lines += (Select-String -Path $latest.FullName -Pattern "PawnIO|Registering RGB controller|Corsair|DRAM" | ForEach-Object { $_.Line.Trim() })

$lines += "`n-- restart FanControl --"
try {
    Start-ScheduledTask -TaskName "FanControl" -ErrorAction Stop
    Start-Sleep -Seconds 4
    $fc = Get-Process -Name FanControl -ErrorAction SilentlyContinue
    $lines += "FanControl running=$([bool]$fc) pid=$($fc.Id -join ',')"
} catch {
    $lines += "task failed: $($_.Exception.Message)"
    Start-Process "C:\Program Files (x86)\FanControl\FanControl.exe" -ErrorAction SilentlyContinue
    $lines += "launched exe directly"
}

$lines | Out-File $out -Encoding utf8
