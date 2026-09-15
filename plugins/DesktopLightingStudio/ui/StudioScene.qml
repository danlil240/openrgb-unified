import QtQuick
import QtQuick3D
import QtQuick3D.Helpers

Rectangle {
    id: root
    color: "#101014"

    // Diagnostic state readable from the probe / debug overlays.
    property string dbg: ""
    property real camYaw: cameraOrigin.eulerRotation.y

    // Canonical body specs. The bridge publishes the resolved body
    // size (obj.bx/by/bz): authored size_m with a per-axis fallback
    // to the geometry's canonical size — a 0 component means
    // "canonical axis" (the contract is owned by ResolvedBodySize in
    // the scene core so schema, validator, and renderer agree).
    // #Cube/#Cylinder/#Sphere are 100-unit primitives, so scale =
    // meters / 100. transform.scale stays dimensionless on the node.
    function bodySpec(obj) {
        switch (obj.geometry) {
        case "desk":          return { src: "#Cube",     c: "#4a3b32" }
        case "case_shell":    return { src: "#Cube",     c: "#e8e8ec", ghost: true }
        case "monitor":       return { src: "#Cube",     c: "#0a0a0c" }
        case "mouse_body":    return { src: "#Cube",     c: "#22222a" }
        case "gpu_body":      return { src: "#Cube",     c: "#e8e8ec" }
        case "keyboard_body": return { src: "#Cube",     c: "#1c1c22" }
        case "fan_body":      return { src: "#Cylinder", c: "#202028" }
        case "ram_body":      return { src: "#Cube",     c: "#18181f" }
        case "pump_body":     return { src: "#Cylinder", c: "#22242c" }
        default:              return null
        }
    }

    // object id -> Node. The delegate reparents through this map so
    // the QML node tree composes exactly like the core's resolved
    // world matrices (parentWorld * local).
    property var objNodes: ({})
    function fixupParents() {
        var objs = (typeof bridge !== "undefined") ? bridge.objectList : []
        for (var i = 0; i < objs.length; i++) {
            var n = objNodes[objs[i].id]
            if (!n) continue
            var p = (objs[i].parentId && objNodes[objs[i].parentId])
            n.parent = p ? p : sceneRoot
        }
    }

    function emitterSize(obj) {
        return obj.geometry === "keyboard_body" ? 0.006 : 0.011
    }

    View3D {
        id: view
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#101014"
            antialiasingMode: SceneEnvironment.NoAA
        }

        // OrbitCameraController requires `origin` to be a Node whose
        // eulerRotation it rotates; the camera must be its child.
        Node {
            id: cameraOrigin
            position: Qt.vector3d(0.1, 0.18, 0.05)
            eulerRotation.x: -38

            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 0, 1.21)
                clipNear: 0.01
                clipFar: 100
            }
        }

        DirectionalLight {
            eulerRotation.x: -35
            eulerRotation.y: -25
            brightness: 1.1
        }

        PointLight {
            position: Qt.vector3d(0.4, 0.6, 0.5)
            brightness: 6
            color: "#9090b0"
        }

        // Scene objects from the bridge — one Node per object,
        // parented per parent_id under this scene root.
        Node {
            id: sceneRoot

        Repeater3D {
            id: objRepeater
            model: (typeof bridge !== "undefined") ? bridge.objectList : []

            // New model = new delegate set; drop stale node refs.
            onModelChanged: root.objNodes = ({})

            delegate: Node {
                id: objNode
                property var obj: modelData
                property var spec: root.bodySpec(obj)
                // Structure (positions) and colors are split: the
                // Repeater's model is emitterPos, which only changes on
                // a scene/layout change, so delegates persist. Per-tick
                // updates only reassign emitterColors — bindings
                // re-evaluate in place instead of rebuilding ~300
                // emitter Models every frame.
                property var emitterPos: []
                property var emitterColors: []

                function reloadLayout() {
                    var list = (typeof bridge !== "undefined") ? bridge.emittersOf(obj.id) : []
                    emitterPos = list
                    var cols = []
                    for (var i = 0; i < list.length; i++) cols.push(list[i].c)
                    emitterColors = cols
                }

                function reloadColors() {
                    emitterColors = (typeof bridge !== "undefined") ? bridge.emitterColorsOf(obj.id) : []
                }

                position: Qt.vector3d(obj.x, obj.y, obj.z)
                // One shared rotation convention: the core converts the
                // stored XYZ degrees (Rz*Ry*Rx) into this quaternion —
                // eulerRotation here would silently apply ZXY instead.
                rotation: Qt.quaternion(obj.qw, obj.qx, obj.qy, obj.qz)
                scale: Qt.vector3d(obj.sx, obj.sy, obj.sz)
                visible: obj.visible

                Component.onCompleted: {
                    root.objNodes[obj.id] = objNode
                    var p = root.objNodes[obj.parentId]
                    objNode.parent = p ? p : sceneRoot
                    reloadLayout()
                    // Last delegate done — repair any parent that was
                    // created after its child.
                    if (Object.keys(root.objNodes).length === objRepeater.count)
                        root.fixupParents()
                }
                Component.onDestruction: {
                    if (root.objNodes[obj.id] === objNode)
                        delete root.objNodes[obj.id]
                }

                Connections {
                    target: (typeof bridge !== "undefined") ? bridge : null
                    function onEmittersChanged(changedId) {
                        if (changedId === objNode.obj.id) objNode.reloadColors()
                    }
                    function onSceneChanged() { objNode.reloadLayout() }
                }

                // Body mesh — ghost shells (the case) are translucent
                // containers; leaving them pickable would swallow every
                // pick aimed at hardware inside them.
                Model {
                    objectName: "obj|" + objNode.obj.id
                    pickable: !(objNode.spec && objNode.spec.ghost)
                    visible: objNode.spec !== null
                    source: objNode.spec ? objNode.spec.src : "#Cube"
                    scale: objNode.spec
                           ? Qt.vector3d(objNode.obj.bx / 100,
                                         objNode.obj.by / 100,
                                         objNode.obj.bz / 100)
                           : Qt.vector3d(0, 0, 0)
                    materials: PrincipledMaterial {
                        baseColor: objNode.spec ? objNode.spec.c : "#000000"
                        opacity: (objNode.spec && objNode.spec.ghost)
                                 ? ((typeof bridge !== "undefined" && bridge.caseGhost) ? 0.12 : 0.42)
                                 : 1.0
                    }
                }

                // Emitter dots (local frame — inherits node transform)
                Repeater3D {
                    model: objNode.emitterPos
                    delegate: Node {
                        required property var modelData

                        // Visible dot — not pickable itself (too small);
                        // the proxy below handles picking.
                        Model {
                            source: "#Sphere"
                            position: Qt.vector3d(modelData.x, modelData.y, modelData.z)
                            property real d: root.emitterSize(objNode.obj) / 100
                            scale: Qt.vector3d(d, d, d)
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                property color c: (modelData.i < objNode.emitterColors.length)
                                                  ? objNode.emitterColors[modelData.i] : "#000000"
                                baseColor: (objNode.obj.bound === "ok" || objNode.obj.bound === "none")
                                           ? c : Qt.darker(c, 2.5)
                            }
                        }

                        // Invisible pick proxy ~2.6x the dot so LEDs are
                        // actually hittable with a mouse.
                        Model {
                            objectName: "emit|" + objNode.obj.id + "|" + modelData.i
                            pickable: true
                            source: "#Sphere"
                            position: Qt.vector3d(modelData.x, modelData.y, modelData.z)
                            property real pd: root.emitterSize(objNode.obj) * 2.6 / 100
                            scale: Qt.vector3d(pd, pd, pd)
                            castsShadows: false
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                baseColor: "#00000000"
                                opacity: 0.0
                                depthDrawMode: PrincipledMaterial.NeverDepthDraw
                            }
                        }
                    }
                }

                // Selection marker
                Model {
                    visible: (typeof bridge !== "undefined") && bridge.selectedId === objNode.obj.id
                    source: "#Sphere"
                    position: Qt.vector3d(0, 0.09, 0)
                    scale: Qt.vector3d(0.00014, 0.00014, 0.00014)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: "#ffd040"
                    }
                }
            }
        }
        }

        OrbitCameraController {
            anchors.fill: parent
            camera: camera
            origin: cameraOrigin
            panEnabled: true
            xSpeed: 0.12
            ySpeed: 0.12

            // Shift+click an emitter dot to paint it.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.ShiftModifier
                onTapped: function(eventPoint) {
                    var result = view.pick(eventPoint.position.x, eventPoint.position.y)
                    if (!result.objectHit || typeof bridge === "undefined") {
                        return
                    }
                    var name = result.objectHit.objectName
                    root.dbg = "shift-tap " + name
                    if (name.indexOf("emit|") === 0) {
                        var p = name.split("|")
                        bridge.paintEmitter(p[1], parseInt(p[2]), bridge.paintColor)
                    }
                }
            }

            // Plain click selects the hit device or emitter's object.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.NoModifier
                onTapped: function(eventPoint) {
                    var result = view.pick(eventPoint.position.x, eventPoint.position.y)
                    root.dbg = "tap hit=" + (result.objectHit ? result.objectHit.objectName : "none")
                    if (!result.objectHit || typeof bridge === "undefined") {
                        return
                    }
                    var name = result.objectHit.objectName
                    if (name.indexOf("emit|") === 0) {
                        bridge.select(name.split("|")[1])
                    } else if (name.indexOf("obj|") === 0) {
                        bridge.select(name.substring(4))
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
        text: (typeof bridge !== "undefined" && bridge.selectedId !== "")
              ? "selected: " + bridge.selectedId
              : "drag: orbit  ·  wheel: zoom  ·  click: select  ·  shift+click LED: paint"
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        visible: typeof bridge !== "undefined" && bridge.live
        color: "#401014"
        radius: 4
        width: liveText.width + 16
        height: liveText.height + 8
        Text {
            id: liveText
            anchors.centerIn: parent
            color: "#ff6060"
            font.pixelSize: 12
            font.bold: true
            text: "LIVE"
        }
    }
}
