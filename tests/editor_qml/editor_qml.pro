#-----------------------------------------------------------
# Editor QML interaction test (Task 2.2)
#
# Drives a REAL SceneBridge + the REAL ui/StudioScene.qml in
# a QQuickWidget. Pointer routing is exercised through the
# SelectionController test seam (beginPressAt/dragTo/
# endGesture take explicit hit info), plus a synthesized
# QMouseEvent middle-drag for end-to-end coverage.
#-----------------------------------------------------------

QT += core gui widgets quickwidgets quick qml quick3d testlib
CONFIG += c++17 console
CONFIG -= debug
CONFIG += release
TARGET = editor_qml_test

win32:LIBS += -lole32 -luser32 -lgdi32

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += $$STUDIO ../../OpenRGB ../../OpenRGB/RGBController \
               ../../OpenRGB/dependencies/json
STUDIO_UI_DIR = $$PWD/../../plugins/DesktopLightingStudio/ui
DEFINES += "STUDIO_UI_DIR=\\\"$$STUDIO_UI_DIR\\\""

HEADERS += $$STUDIO/plugin/SceneBridge.h \
    $$STUDIO/config/ConfigStore.h \
    $$STUDIO/editor/SceneObjectModel.h \
    $$STUDIO/inputs/ScreenSampler.h

SOURCES += main.cpp \
    $$STUDIO/plugin/SceneBridge.cpp \
    $$STUDIO/config/StudioConfig.cpp \
    $$STUDIO/config/ConfigStore.cpp \
    $$STUDIO/config/ConfigMigration.cpp \
    $$STUDIO/editor/EditorController.cpp \
    $$STUDIO/editor/TransformCommands.cpp \
    $$STUDIO/editor/SceneObjectModel.cpp \
    $$STUDIO/output/ControllerAdapter.cpp \
    $$STUDIO/presets/DevicePreset.cpp \
    $$STUDIO/presets/PresetRegistry.cpp \
    $$STUDIO/scene/SceneTypes.cpp \
    $$STUDIO/scene/SceneGraph.cpp \
    $$STUDIO/scene/EmitterLayout.cpp \
    $$STUDIO/scene/SceneJson.cpp \
    $$STUDIO/scene/SceneResolver.cpp \
    $$STUDIO/scene/BindingResolver.cpp \
    $$STUDIO/scene/DefaultDesk.cpp \
    $$STUDIO/effects/EffectTypes.cpp \
    $$STUDIO/effects/EffectEngine.cpp \
    $$STUDIO/effects/Presets.cpp \
    $$STUDIO/inputs/InputBus.cpp \
    $$STUDIO/inputs/OnsetDetect.cpp \
    $$STUDIO/inputs/KeyMap.cpp \
    $$STUDIO/inputs/AudioLoopback.cpp \
    $$STUDIO/inputs/KeyHook.cpp \
    $$STUDIO/inputs/ScreenSampler.cpp
