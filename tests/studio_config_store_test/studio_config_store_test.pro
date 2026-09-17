#-----------------------------------------------------------
# Studio config store test
#
# Exercises config/ConfigStore (QSaveFile atomic saves,
# last-valid backup, debounced autosave, external-change
# detection, recovery) against a temp workspace dir.
# QtCore + Testlib only; runs headless.
#-----------------------------------------------------------

QT += core testlib
CONFIG += c++17 console
CONFIG -= debug
CONFIG += release
TARGET = studio_config_store_test

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += $$STUDIO ../../OpenRGB/dependencies/json

HEADERS += \
    $$STUDIO/config/ConfigStore.h \
    $$STUDIO/presets/DevicePreset.h \
    $$STUDIO/presets/PresetRegistry.h \
    $$STUDIO/scene/SceneResolver.h

SOURCES += main.cpp \
    $$STUDIO/config/ConfigStore.cpp \
    $$STUDIO/config/StudioConfig.cpp \
    $$STUDIO/config/ConfigMigration.cpp \
    $$STUDIO/presets/DevicePreset.cpp \
    $$STUDIO/presets/PresetRegistry.cpp \
    $$STUDIO/scene/SceneResolver.cpp \
    $$STUDIO/scene/SceneTypes.cpp \
    $$STUDIO/scene/SceneGraph.cpp \
    $$STUDIO/scene/EmitterLayout.cpp \
    $$STUDIO/scene/SceneJson.cpp \
    $$STUDIO/scene/BindingResolver.cpp \
    $$STUDIO/scene/DefaultDesk.cpp \
    $$STUDIO/effects/EffectTypes.cpp \
    $$STUDIO/effects/EffectEngine.cpp \
    $$STUDIO/effects/EffectJson.cpp \
    $$STUDIO/presets/EffectRegistry.cpp \
    $$STUDIO/effects/Presets.cpp \
    $$STUDIO/inputs/InputBus.cpp \
    $$STUDIO/inputs/OnsetDetect.cpp \
    $$STUDIO/inputs/KeyMap.cpp

RESOURCES += schema.qrc
