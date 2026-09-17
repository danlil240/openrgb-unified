#!/usr/bin/env bash
#-----------------------------------------------------------#
# DesktopLightingStudio packaging verification (posix)      #
#                                                           #
# Checks the file-side manifest in PACKAGING.md against     #
# out/ — the shared object must exist and be non-empty.     #
# Qt/QML runtime rows are Windows-only (windeployqt); on    #
# posix the runtime deps come from the host's Qt install,   #
# so this checks the plugin binary plus the loose ui/ tree  #
# that deploy-openrgb.sh ships.                             #
# Read-only: never copies, never deploys.                   #
#-----------------------------------------------------------#
set -u
cd "$(dirname "$0")"
MISSING=0
CHECKED=0

need_file() {
    CHECKED=$((CHECKED + 1))
    if [ ! -f "$1" ]; then
        echo "  MISSING $1"; MISSING=$((MISSING + 1)); return
    fi
    if [ ! -s "$1" ]; then
        echo "  EMPTY   $1"; MISSING=$((MISSING + 1)); return
    fi
    echo "  ok      $1"
}

need_dir() {
    CHECKED=$((CHECKED + 1))
    if [ -d "$1" ]; then
        echo "  ok      $1/"
    else
        echo "  MISSING $1/"; MISSING=$((MISSING + 1))
    fi
}

# The plugin itself — whichever flavor this OS produced.
so=""
for f in out/libDesktopLightingStudio.so out/libDesktopLightingStudio.dylib; do
    [ -f "$f" ] && so="$f" && break
done
CHECKED=$((CHECKED + 1))
if [ -z "$so" ]; then
    echo "  MISSING out/libDesktopLightingStudio.{so,dylib}"
    MISSING=$((MISSING + 1))
elif [ ! -s "$so" ]; then
    echo "  EMPTY   $so"
    MISSING=$((MISSING + 1))
else
    echo "  ok      $so"
fi

# Loose ui/ override tree — the deployed QML payload.
need_dir ui
for d in ui/components ui/editor ui/devices ui/materials; do
    need_dir "$d"
done
for f in ui/StudioScene.qml ui/StudioWorkspace.qml ui/studio.qrc \
         plugin/metadata.json; do
    need_file "$f"
done

echo ------------------------------------------------------------
if [ "$MISSING" -eq 0 ]; then
    echo "PACKAGE OK - $CHECKED manifest entries present"
    exit 0
fi
echo "PACKAGE INCOMPLETE - $MISSING of $CHECKED manifest entries missing"
exit 1
