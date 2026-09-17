#!/usr/bin/env bash
# Build the plugin into out/ — mirrors build-only.bat (no deploy).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build out
QMAKE="$(command -v qmake6 || command -v qmake6-qt6 || command -v qmake)"
echo "using $QMAKE"
"$QMAKE" DesktopLightingStudio.pro -o build/Makefile
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu)"
make -C build -j"$JOBS"
ls -la out/
