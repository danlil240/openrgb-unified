QT += widgets quickwidgets quick qml gui quick3d
CONFIG += c++17 console
TARGET = quickwidget_probe

win32:LIBS += -lole32 -luser32 -lgdi32

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += $$STUDIO ../../OpenRGB ../../OpenRGB/RGBController \
               ../../OpenRGB/dependencies/json

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
