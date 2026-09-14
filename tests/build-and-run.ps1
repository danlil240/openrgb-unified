# Builds and runs the offline Lian Li wireless protocol tests.
# Uses MSVC Build Tools (cl) only — no Qt, no hardware.
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$ctrl   = Join-Path $root 'OpenRGB\Controllers\LianLiWirelessController'
$vendor = Join-Path $root 'OpenRGB\dependencies'
$out    = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null

# Locate cl via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC Build Tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$sources = @(
    "$PSScriptRoot\lianli_protocol_test.cpp",
    "$ctrl\LianLiWirelessProtocol.cpp",
    "$ctrl\LianLiWirelessCodec.cpp",
    "$ctrl\LianLiWirelessRuntime.cpp",
    "$vendor\tinyuz\compress\tuz_enc.cpp",
    "$vendor\tinyuz\compress\tuz_enc_private\tuz_enc_clip.cpp",
    "$vendor\tinyuz\compress\tuz_enc_private\tuz_enc_code.cpp",
    "$vendor\tinyuz\compress\tuz_enc_private\tuz_enc_match.cpp",
    "$vendor\tinyuz\compress\tuz_enc_private\tuz_sstring.cpp",
    "$vendor\tinyuz\decompress\tuz_dec.c",
    "$vendor\HDiffPatch\libHDiffPatch\HDiff\private_diff\libdivsufsort\divsufsort.cpp"
)

$srcArgs = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$outFwd = $out -replace '\\','/'
$cmd = "call `"$vcvars`" >nul 2>&1 && cl /nologo /EHsc /O2 /std:c++17 /DNDEBUG /D_IS_USED_MULTITHREAD=0 /I`"$ctrl`" /I`"$vendor`" /I`"$vendor\tinyuz`" /I`"$vendor\HDiffPatch`" /Fo`"$outFwd/`" /Fe:`"$outFwd/lianli_protocol_test.exe`" $srcArgs"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }

& "$out\lianli_protocol_test.exe"
exit $LASTEXITCODE
