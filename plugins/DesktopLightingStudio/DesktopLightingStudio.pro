#-----------------------------------------------------------
# Desktop Lighting Studio — OpenRGB API-5 plugin
#
# Stage 0 probe: minimal Studio tab hosting a Qt Quick 3D
# scene inside a QWidget, used to validate rendering,
# picking, and packaged QML module loading in the host.
#-----------------------------------------------------------

QT          += core gui widgets quick quickwidgets quick3d

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
    plugin/StudioTab.h

SOURCES += \
    plugin/DesktopLightingStudio.cpp \
    plugin/StudioTab.cpp

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
