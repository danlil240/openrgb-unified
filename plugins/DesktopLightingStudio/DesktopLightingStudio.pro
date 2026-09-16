#-----------------------------------------------------------
# Desktop Lighting Studio — OpenRGB API-5 plugin
#
# Stage 1: scene-model-driven desk view with bound devices,
# emitter rendering, static color control, and live output.
#-----------------------------------------------------------

QT          += core gui widgets quick quickwidgets quick3d

# WASAPI loopback + LL key hook + GDI screen grab (Stage 3 inputs)
win32:LIBS  += -lole32 -luser32 -lgdi32

TEMPLATE     = lib
CONFIG      += plugin c++17 release
CONFIG      -= debug debug_and_release
TARGET       = DesktopLightingStudio

OPENRGB_ROOT = $$PWD/../../OpenRGB

INCLUDEPATH += \
    $$OPENRGB_ROOT \
    $$OPENRGB_ROOT/RGBController \
    $$OPENRGB_ROOT/dependencies/json

HEADERS += \
    plugin/DesktopLightingStudio.h \
    plugin/StudioTab.h \
    plugin/SceneBridge.h \
    config/StudioConfig.h \
    config/ConfigStore.h \
    config/ConfigMigration.h \
    editor/EditorController.h \
    editor/TransformCommands.h \
    editor/SceneObjectModel.h \
    editor/PresetListModel.h \
    output/ControllerAdapter.h \
    presets/DevicePreset.h \
    presets/PresetRegistry.h \
    scene/SceneTypes.h \
    scene/SceneGraph.h \
    scene/JsonFields.h \
    scene/EmitterLayout.h \
    scene/SceneJson.h \
    scene/SceneResolver.h \
    scene/BindingResolver.h \
    scene/DefaultDesk.h \
    effects/EffectTypes.h \
    effects/EffectEngine.h \
    effects/Presets.h \
    inputs/InputBus.h \
    inputs/OnsetDetect.h \
    inputs/KeyMap.h \
    inputs/AudioLoopback.h \
    inputs/KeyHook.h \
    inputs/ScreenSampler.h

SOURCES += \
    plugin/DesktopLightingStudio.cpp \
    plugin/StudioTab.cpp \
    plugin/SceneBridge.cpp \
    config/StudioConfig.cpp \
    config/ConfigStore.cpp \
    config/ConfigMigration.cpp \
    editor/EditorController.cpp \
    editor/TransformCommands.cpp \
    editor/SceneObjectModel.cpp \
    editor/PresetListModel.cpp \
    output/ControllerAdapter.cpp \
    presets/DevicePreset.cpp \
    presets/PresetRegistry.cpp \
    scene/SceneTypes.cpp \
    scene/SceneGraph.cpp \
    scene/EmitterLayout.cpp \
    scene/SceneJson.cpp \
    scene/SceneResolver.cpp \
    scene/BindingResolver.cpp \
    scene/DefaultDesk.cpp \
    effects/EffectTypes.cpp \
    effects/EffectEngine.cpp \
    effects/Presets.cpp \
    inputs/InputBus.cpp \
    inputs/OnsetDetect.cpp \
    inputs/KeyMap.cpp \
    inputs/AudioLoopback.cpp \
    inputs/KeyHook.cpp \
    inputs/ScreenSampler.cpp

RESOURCES += \
    ui/studio.qrc

DISTFILES += \
    plugin/metadata.json \
    ui/StudioScene.qml \
    ui/StudioWorkspace.qml \
    ui/components/Theme.qml \
    ui/components/DeviceTree.qml \
    ui/components/DeviceLibrary.qml \
    ui/components/DevicePresetEditor.qml \
    ui/components/Inspector.qml \
    ui/components/LookShelf.qml \
    ui/editor/CameraController.qml \
    ui/editor/SelectionController.qml \
    ui/editor/TransformGizmo.qml \
    ui/editor/TransformInspector.qml \
    ui/devices/Keyboard.qml \
    ui/devices/Mouse.qml \
    ui/devices/Fan.qml \
    ui/devices/Ram.qml \
    ui/devices/Case.qml \
    ui/devices/Gpu.qml \
    ui/devices/Monitor.qml \
    ui/devices/Strip.qml \
    ui/devices/SelectionFrame.qml \
    ui/materials/StudioEnvironment.qml \
    schemas/studio.schema.json \
    schemas/device.schema.json \
    presets/devices/desk.device.json \
    presets/devices/monitor.device.json \
    presets/devices/pc-case.device.json \
    presets/devices/keyboard-104.device.json \
    presets/devices/mouse-3zone.device.json \
    presets/devices/fan-120.device.json \
    presets/devices/fan-slw.device.json \
    presets/devices/pump-360.device.json \
    presets/devices/gpu-fan.device.json \
    presets/devices/gpu-card.device.json \
    presets/devices/gpu-logo.device.json \
    presets/devices/ram-stick.device.json \
    presets/devices/group.device.json

DESTDIR      = $$PWD/out
OBJECTS_DIR  = $$PWD/build/obj
MOC_DIR      = $$PWD/build/moc
RCC_DIR      = $$PWD/build/rcc
UI_DIR       = $$PWD/build/ui
