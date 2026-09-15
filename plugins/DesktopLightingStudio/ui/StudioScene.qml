import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Rectangle {
    id: root
    color: "#101014"

    View3D {
        id: view
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#101014"
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            id: camera
            position: Qt.vector3d(0, 0.9, 1.5)
            eulerRotation.x: -30
            clipNear: 0.01
            clipFar: 100
        }

        DirectionalLight {
            eulerRotation.x: -35
            eulerRotation.y: -25
            brightness: 1.2
        }

        PointLight {
            position: Qt.vector3d(0.5, 0.5, 0.5)
            brightness: 8
            color: "#7070ff"
        }

        // Desk surface (1.4 m x 0.7 m)
        Model {
            objectName: "desk"
            pickable: true
            source: "#Cube"
            position: Qt.vector3d(0, -0.01, 0.1)
            scale: Qt.vector3d(14, 0.2, 7)
            materials: PrincipledMaterial { baseColor: "#4a3b32" }
        }

        // PC case — right side of the desk
        Model {
            objectName: "case"
            pickable: true
            source: "#Cube"
            position: Qt.vector3d(0.48, 0.225, -0.05)
            scale: Qt.vector3d(2.1, 4.5, 4.6)
            materials: PrincipledMaterial {
                baseColor: "#e8e8ec"
                opacity: 0.35
            }
            opacity: 0.35
        }

        // Three fan rings inside the case (emissive, facing the camera)
        Repeater3D {
            model: 3
            Model {
                objectName: "case fan " + index
                pickable: true
                source: "#Cylinder"
                position: Qt.vector3d(0.37, 0.10 + index * 0.125, 0.06)
                eulerRotation.x: 90
                scale: Qt.vector3d(0.6, 0.15, 0.6)
                materials: PrincipledMaterial {
                    baseColor: "#202028"
                    emissiveFactor: Qt.vector3d(0.2, 0.9, 1.0)
                }
            }
        }

        // Keyboard in front of the case
        Model {
            objectName: "keyboard"
            pickable: true
            source: "#Cube"
            position: Qt.vector3d(-0.15, 0.02, 0.28)
            scale: Qt.vector3d(3.6, 0.25, 1.3)
            materials: PrincipledMaterial {
                baseColor: "#1c1c22"
                emissiveFactor: Qt.vector3d(0.05, 0.15, 0.35)
            }
        }

        // Keyboard per-key hint strip
        Model {
            objectName: "keyboard led strip"
            pickable: true
            source: "#Cube"
            position: Qt.vector3d(-0.15, 0.036, 0.28)
            scale: Qt.vector3d(3.4, 0.03, 1.1)
            materials: PrincipledMaterial {
                baseColor: "#101018"
                emissiveFactor: Qt.vector3d(0.3, 0.1, 0.8)
            }
        }

        // Mouse to the right of the keyboard
        Model {
            objectName: "mouse"
            pickable: true
            source: "#Sphere"
            position: Qt.vector3d(0.22, 0.025, 0.30)
            scale: Qt.vector3d(0.7, 0.35, 1.0)
            materials: PrincipledMaterial {
                baseColor: "#22222a"
                emissiveFactor: Qt.vector3d(0.6, 0.05, 0.05)
            }
        }

        // Two RAM sticks standing on the case's motherboard area
        Repeater3D {
            model: 2
            Model {
                objectName: "ram " + index
                pickable: true
                source: "#Cube"
                position: Qt.vector3d(0.415 + index * 0.03, 0.33, 0.02)
                scale: Qt.vector3d(0.12, 0.5, 0.04)
                materials: PrincipledMaterial {
                    baseColor: "#18181f"
                    emissiveFactor: Qt.vector3d(0.9, 0.5, 0.1)
                }
            }
        }

        OrbitCameraController {
            anchors.fill: parent
            camera: camera
            origin: Qt.vector3d(0, 0.18, 0.05)
            panEnabled: true
            xSpeed: 140
            ySpeed: 140

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: function(eventPoint) {
                    var result = view.pick(eventPoint.position.x, eventPoint.position.y)
                    if(result.objectHit) {
                        pickedLabel.text = "picked: " + result.objectHit.objectName
                            + "  @  " + result.scenePosition.x.toFixed(2) + ", "
                            + result.scenePosition.y.toFixed(2) + ", "
                            + result.scenePosition.z.toFixed(2)
                    } else {
                        pickedLabel.text = "picked: (nothing)"
                    }
                }
            }
        }
    }

    Text {
        id: pickedLabel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        color: "#d8d8e0"
        font.pixelSize: 14
        text: "Stage 0 probe — orbit: drag, zoom: wheel, pick: click"
    }
}
