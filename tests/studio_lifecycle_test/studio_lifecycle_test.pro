#-----------------------------------------------------------
# Studio output-lifecycle test (Task 6.1)
#
# Drives a REAL SceneBridge against a fake OpenRGB plugin API
# + fake RGBControllerInterface — no hardware, no QML. Covers:
# live push start/stop/restart, probe pause/resume (incl.
# overlapping probes), destruction with a lane worker in
# flight, pausePushes() failing once shutdown began, driver
# throws not wedging lanes, hidden-preview gating, rescan
# controller swap, and repeated create/destroy cycles.
#-----------------------------------------------------------

QT += core gui widgets testlib
CONFIG += c++17 console
CONFIG -= debug
CONFIG += release
TARGET = studio_lifecycle_test

win32:LIBS += -lole32 -luser32 -lgdi32

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += $$STUDIO ../../OpenRGB ../../OpenRGB/RGBController \
               ../../OpenRGB/dependencies/json

HEADERS += $$STUDIO/plugin/SceneBridge.h \
    $$STUDIO/config/ConfigStore.h \
    $$STUDIO/editor/SceneObjectModel.h \
    $$STUDIO/editor/PresetListModel.h \
    $$STUDIO/editor/EffectLayerModel.h \
    $$STUDIO/presets/PresetBundle.h \
    $$STUDIO/inputs/ScreenSampler.h

SOURCES += main.cpp \
    $$STUDIO/plugin/SceneBridge.cpp \
    $$STUDIO/config/StudioConfig.cpp \
    $$STUDIO/config/ConfigStore.cpp \
    $$STUDIO/config/ConfigMigration.cpp \
    $$STUDIO/editor/EditorController.cpp \
    $$STUDIO/editor/TransformCommands.cpp \
    $$STUDIO/editor/SceneObjectModel.cpp \
    $$STUDIO/editor/PresetListModel.cpp \
    $$STUDIO/editor/EffectLayerModel.cpp \
    $$STUDIO/output/ControllerAdapter.cpp \
    $$STUDIO/presets/DevicePreset.cpp \
    $$STUDIO/presets/PresetBundle.cpp \
    $$STUDIO/presets/PresetRegistry.cpp \
    $$STUDIO/presets/EffectRegistry.cpp \
    $$STUDIO/scene/SceneTypes.cpp \
    $$STUDIO/scene/SceneGraph.cpp \
    $$STUDIO/scene/EmitterLayout.cpp \
    $$STUDIO/scene/SceneJson.cpp \
    $$STUDIO/scene/SceneResolver.cpp \
    $$STUDIO/scene/BindingResolver.cpp \
    $$STUDIO/scene/DefaultDesk.cpp \
    $$STUDIO/effects/EffectTypes.cpp \
    $$STUDIO/effects/EffectEngine.cpp \
    $$STUDIO/effects/EffectJson.cpp \
    $$STUDIO/effects/Presets.cpp \
    $$STUDIO/inputs/InputBus.cpp \
    $$STUDIO/inputs/OnsetDetect.cpp \
    $$STUDIO/inputs/KeyMap.cpp \
    $$STUDIO/inputs/AudioLoopback.cpp \
    $$STUDIO/inputs/KeyHook.cpp \
    $$STUDIO/inputs/ScreenSampler.cpp

# The bridge reads the packaged type/look libraries from
# :/studio/presets/** — without the plugin qrc the harness
# resolves on the desk-only fallback set and no bound device
# exists to push.
RESOURCES += $$STUDIO/ui/studio.qrc
