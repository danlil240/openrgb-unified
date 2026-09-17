#!/usr/bin/env bash
# Deploy the built plugin into the host's user plugins dir.
set -euo pipefail
cd "$(dirname "$0")"
dest="${XDG_CONFIG_HOME:-$HOME/.config}/OpenRGB/plugins/DesktopLightingStudio"
mkdir -p "$dest"
so="$(ls out/libDesktopLightingStudio.so out/libDesktopLightingStudio.dylib 2>/dev/null | head -1)"
[ -n "$so" ] || { echo "no built plugin in out/ — run build-plugin.sh first"; exit 1; }
cp "$so" "$dest/"
echo "deployed $so -> $dest"
echo "NOTE: the host OpenRGB needs Qt 6.8+ QML runtime modules (Quick3D)."
