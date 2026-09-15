QT += widgets quickwidgets quick qml gui
CONFIG += c++17 console
TARGET = quickwidget_probe

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += ../../OpenRGB ../../OpenRGB/RGBController ../../OpenRGB/dependencies/json

HEADERS += $$STUDIO/plugin/SceneBridge.h

SOURCES += main.cpp \
    $$STUDIO/plugin/SceneBridge.cpp \
    $$STUDIO/output/ControllerAdapter.cpp \
    $$STUDIO/scene/SceneTypes.cpp \
    $$STUDIO/scene/EmitterLayout.cpp \
    $$STUDIO/scene/SceneJson.cpp \
    $$STUDIO/scene/BindingResolver.cpp \
    $$STUDIO/scene/DefaultDesk.cpp
