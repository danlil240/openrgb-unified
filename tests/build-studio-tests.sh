#!/usr/bin/env bash
# Build + run the Qt-free Desktop Lighting Studio scene/config tests.
# Posix mirror of build-studio-tests.bat — same suites, same source
# lists, system compiler instead of cl.
set -euo pipefail
cd "$(dirname "$0")"
STUDIO=../plugins/DesktopLightingStudio
JSON=../OpenRGB/dependencies/json
CXX="${CXX:-c++}"
mkdir -p out/scene out/config out/presets out/editor out/effects

# Shared source list — scene core + effects + the v3 type stack
# (presets, resolver, migration) so every suite sees one truth.
SCENE_SRC=(
   "$STUDIO/scene/SceneTypes.cpp"
   "$STUDIO/scene/SceneGraph.cpp"
   "$STUDIO/scene/EmitterLayout.cpp"
   "$STUDIO/scene/SceneJson.cpp"
   "$STUDIO/scene/BindingResolver.cpp"
   "$STUDIO/scene/DefaultDesk.cpp"
   "$STUDIO/effects/EffectTypes.cpp"
   "$STUDIO/effects/EffectEngine.cpp"
   "$STUDIO/effects/EffectJson.cpp"
   "$STUDIO/effects/Presets.cpp"
   "$STUDIO/inputs/InputBus.cpp"
   "$STUDIO/inputs/OnsetDetect.cpp"
   "$STUDIO/inputs/KeyMap.cpp"
)
V3_SRC=(
   "$STUDIO/presets/DevicePreset.cpp"
   "$STUDIO/presets/PresetRegistry.cpp"
   "$STUDIO/presets/EffectRegistry.cpp"
   "$STUDIO/presets/PresetBundle.cpp"
   "$STUDIO/scene/SceneResolver.cpp"
   "$STUDIO/config/StudioConfig.cpp"
   "$STUDIO/config/ConfigMigration.cpp"
)
# Qt-free editor core — SceneObjectModel stays plugin-only (Qt).
EDITOR_SRC=(
   "$STUDIO/editor/EditorController.cpp"
   "$STUDIO/editor/TransformCommands.cpp"
)

# Static C++ runtime on Windows hosts: the produced exe would otherwise
# need libstdc++-6.dll/libgcc from the compiler's bin dir on PATH.
# clang rejects -static-libgcc, so keep it MSYS/Cygwin-only.
STATIC_FLAGS=()
case "$OSTYPE" in
    msys*|cygwin*|win32*) STATIC_FLAGS=(-static-libstdc++ -static-libgcc) ;;
esac
CXXFLAGS=(-std=c++17 "${STATIC_FLAGS[@]}" -I"$STUDIO" -I"$JSON")

"$CXX" "${CXXFLAGS[@]}" studio_scene_test.cpp \
   "${SCENE_SRC[@]}" "${V3_SRC[@]}" \
   -o out/scene/studio_scene_test
./out/scene/studio_scene_test

"$CXX" "${CXXFLAGS[@]}" studio_config_test.cpp \
   "${SCENE_SRC[@]}" "${V3_SRC[@]}" \
   -o out/config/studio_config_test
./out/config/studio_config_test

"$CXX" "${CXXFLAGS[@]}" device_preset_test.cpp \
   "${SCENE_SRC[@]}" "${V3_SRC[@]}" \
   -o out/presets/studio_preset_test
./out/presets/studio_preset_test

"$CXX" "${CXXFLAGS[@]}" studio_editor_test.cpp \
   "${SCENE_SRC[@]}" "${V3_SRC[@]}" "${EDITOR_SRC[@]}" \
   -o out/editor/studio_editor_test
./out/editor/studio_editor_test

"$CXX" "${CXXFLAGS[@]}" effect_json_test.cpp \
   "${SCENE_SRC[@]}" "${V3_SRC[@]}" \
   -o out/effects/effect_json_test
./out/effects/effect_json_test

# Qt-free suites that land after this script; compile when present.
if [ -f key_translate_test.cpp ]; then
    "$CXX" "${CXXFLAGS[@]}" -o out/key_translate_test \
         key_translate_test.cpp \
         "$STUDIO/inputs/KeyTranslate.cpp" "$STUDIO/inputs/KeyMap.cpp"
    ./out/key_translate_test
fi
