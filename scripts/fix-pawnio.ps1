# Restarts the PawnIO PnP device node (ROOT\PAWNIO\0000) so the driver
# re-creates its \Device\PawnIO symlink, then verifies openability and
# re-runs baseline OpenRGB detection. Results -> build-test\elevated\pawnio-fix.txt

$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build-test\elevated"
$out  = Join-Path $work "pawnio-fix.txt"
$exe  = Join-Path $root "OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"

$lines = @("== PawnIO device restart $(Get-Date -Format o) ==")

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "must run elevated"; exit 1
}

$lines += "`n-- device state before --"
$lines += (Get-PnpDevice -InstanceId "ROOT\PAWNIO\0000" | Select-Object Status, FriendlyName | Format-Table | Out-String).Trim()

$lines += "`n-- restarting PawnIO device --"
try {
    Disable-PnpDevice -InstanceId "ROOT\PAWNIO\0000" -Confirm:$false -ErrorAction Stop
    Start-Sleep -Seconds 2
    Enable-PnpDevice -InstanceId "ROOT\PAWNIO\0000" -Confirm:$false -ErrorAction Stop
    Start-Sleep -Seconds 3
    $lines += "disable/enable cycle done"
} catch {
    $lines += "Disable/Enable failed: $($_.Exception.Message); trying pnputil"
    $lines += (pnputil /restart-device "ROOT\PAWNIO\0000" 2>&1 | Out-String)
}

$lines += "`n-- device state after --"
$lines += (Get-PnpDevice -InstanceId "ROOT\PAWNIO\0000" | Select-Object Status, FriendlyName | Format-Table | Out-String).Trim()

$lines += "`n-- direct open test --"
try {
    $fs = [System.IO.File]::Open("\\.\PawnIO", 'Open', 'ReadWrite', 'ReadWrite')
    $lines += "OPEN OK: \\.\PawnIO"
    $fs.Close()
} catch {
    $lines += "OPEN FAIL: \\.\PawnIO -> $($_.Exception.Message)"
}

$lines += "`n-- baseline OpenRGB -ld --"
Get-Process -Name OpenRGB -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
Push-Location $work
try { & $exe --localconfig --noautoconnect --verbose -ld | Out-Null } finally { Pop-Location }
$latest = Get-ChildItem (Join-Path $work "logs") -Filter *.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$lines += "log: $($latest.FullName)"
$lines += (Select-String -Path $latest.FullName -Pattern "PawnIO|Registering RGB controller|Corsair|DRAM" | ForEach-Object { $_.Line.Trim() })

$lines += "`n-- FanControl state --"
$lines += "running=$([bool](Get-Process FanControl -ErrorAction SilentlyContinue))"

$lines | Out-File $out -Encoding utf8
