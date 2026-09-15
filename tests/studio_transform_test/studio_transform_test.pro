#-----------------------------------------------------------
# Studio transform integration test
#
# Verifies that the Qt-free scene graph (SceneGraph) produces
# emitter world positions identical to a real QQuick3DNode
# tree fed the same quaternion the bridge publishes to QML.
# Uses QtQuick3D private headers (QQuick3DNode is private in
# Qt 6.8); builds and runs without windeployqt.
#-----------------------------------------------------------

QT += core gui quick3d quick3d-private
CONFIG += c++17 console
CONFIG -= debug
CONFIG += release
TARGET = studio_transform_test

STUDIO = ../../plugins/DesktopLightingStudio
INCLUDEPATH += $$STUDIO ../../OpenRGB/dependencies/json

SOURCES += main.cpp \
    $$STUDIO/scene/SceneTypes.cpp \
    $$STUDIO/scene/SceneGraph.cpp \
    $$STUDIO/scene/EmitterLayout.cpp \
    $$STUDIO/scene/SceneJson.cpp \
    $$STUDIO/scene/DefaultDesk.cpp
