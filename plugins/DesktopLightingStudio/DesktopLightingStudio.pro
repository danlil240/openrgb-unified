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
    output/ControllerAdapter.h \
    scene/SceneTypes.h \
    scene/SceneGraph.h \
    scene/EmitterLayout.h \
    scene/SceneJson.h \
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
    output/ControllerAdapter.cpp \
    scene/SceneTypes.cpp \
    scene/SceneGraph.cpp \
    scene/EmitterLayout.cpp \
    scene/SceneJson.cpp \
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
    ui/StudioScene.qml

DESTDIR      = $$PWD/out
OBJECTS_DIR  = $$PWD/build/obj
MOC_DIR      = $$PWD/build/moc
RCC_DIR      = $$PWD/build/rcc
UI_DIR       = $$PWD/build/ui
