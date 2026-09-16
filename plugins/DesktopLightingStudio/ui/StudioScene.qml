/*---------------------------------------------------------*\
|| StudioScene.qml                                         ||
||                                                         ||
||   Desk viewport: View3D + object delegates + the M2.2  ||
||   editor layer (CameraController / SelectionController ||
||   / TransformGizmo / TransformInspector).              ||
||                                                         ||
||   Interaction contract (spec §4):                      ||
||     left click           select / empty-space clears   ||
||     left drag, device    move on the active plane      ||
||     left drag, empty     marquee selection             ||
||     middle drag          camera pan — ALWAYS           ||
||     space + left drag    camera pan (alternative)      ||
||     alt + left drag      orbit (free view only)        ||
||     wheel                pointer-centered zoom, clamped||
||     W / E                move / rotate tools           ||
||     Shift during gesture temporary snap 10mm / 15°     ||
||     F / Home             frame selection / frame desk  ||
||     Escape               cancel gesture, restore state ||
||     Ctrl+Z / Ctrl+Shift+Z undo / redo                  ||
||   Paint is an explicit tool: its drags paint emitters  ||
||   and can never move objects or orbit the camera.      ||
\*---------------------------------------------------------*/
import QtQuick
import QtQuick3D
import "editor" as Ed

Rectangle {
    id: root
    color: "#101014"
    focus: true

    // Diagnostic state readable from the probe / debug overlays.
    property string dbg: ""
    property real camYaw: cameraOrigin.eulerRotation.y

    // Canonical body specs — dispatch on the geometry string.
    // #Cube/#Cylinder/#Sphere are 100-unit primitives, so scale =
    // meters / 100. transform.scale stays dimensionless on the node.
    function bodySpec(geom) {
        switch (geom) {
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

    function emitterSize(geom) {
        return geom === "keyboard_body" ? 0.006 : 0.011
    }

    // object id -> delegate Node. The delegate reparents through
    // this map so the QML node tree composes exactly like the
    // core's resolved world matrices (parentWorld * local).
    property var objNodes: ({})
    function fixupParents() {
        for (var id in objNodes) {
            var n = objNodes[id]
            var p = (n.oParent && objNodes[n.oParent])
            n.parent = p ? p : sceneRoot
        }
    }

    // World bounds over the object-node map. ids = instance ids to
    // include; null/empty = the whole scene. Used by F / Home and
    // the case preset view.
    function instanceWorldBounds(ids) {
        var want = null
        if (ids && ids.length) {
            want = {}
            for (var k = 0; k < ids.length; k++)
                want[ids[k]] = true
        }
        var sx = 0, sy = 0, sz = 0, n = 0
        var pts = []
        for (var id in objNodes) {
            var item = objNodes[id]
            if (!item || !item.visible)
                continue
            if (want && !want[item.oInst])
                continue
            var p = item.scenePosition
            pts.push({ p: p, r: 0.5 * Math.max(item.oBx, item.oBy, item.oBz) })
            sx += p.x; sy += p.y; sz += p.z; n++
        }
        if (n === 0)
            return null
        var c = Qt.vector3d(sx / n, sy / n, sz / n)
        var r = 0.05
        for (var i = 0; i < pts.length; i++) {
            var d = pts[i].p.minus(c).length() + pts[i].r
            if (d > r)
                r = d
        }
        return { c: c, r: r }
    }

    // Test seam: world/screen position of an object id's node.
    function worldPosOf(objectId) {
        var n = objNodes[objectId]
        return n ? n.scenePosition : null
    }
    function screenPosOf(objectId) {
        var w = worldPosOf(objectId)
        return (w && view) ? view.mapFrom3DScene(w) : null
    }

    function eachNode(cb) {
        for (var id in objNodes)
            cb(objNodes[id])
    }

    function escapeAll() {
        /* Escape priority: live gesture > text focus > selection. */
        if (selCtl.cancel())
            return
        if (inspector.textFocus) {
            forceActiveFocus()
            return
        }
        if (typeof bridge !== "undefined")
            bridge.clearEditorSelection()
    }

    /* True while any gesture-capable press window is open: the
       click candidate, live pan/orbit/move/rotate/marquee/paint,
       or the bridge's transform gesture. Frame commands emit
       poseFinished under the press's camera snapshot — gate them
       on this the same way undo is gated. */
    function gestureBusy() {
        return selCtl.pressed
               || (typeof bridge !== "undefined" && bridge.gestureActive())
    }

    /* Undo/redo never run under a live gesture — the stack op would
       write beneath the snapshot the gesture is previewing against.
       The bridge slot refuses too (defense for ungated callers);
       gating here lets the swallowed shortcut say WHY via the
       results box. */
    function undoOrRedo(redo) {
        if (typeof bridge === "undefined")
            return
        if (bridge.gestureActive()) {
            bridge.statusMessage("finish the drag first")
            return
        }
        if (redo)
            bridge.redo()
        else
            bridge.undo()
    }

    View3D {
        id: view
        anchors.fill: parent
        /* camCtl is declared further down the file — guard the
           forward id refs so a resize during construction is safe. */
        onHeightChanged: if (typeof camCtl !== "undefined" && camCtl) camCtl.apply()
        onWidthChanged:  if (typeof camCtl !== "undefined" && camCtl) camCtl.apply()

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#101014"
            antialiasingMode: SceneEnvironment.NoAA
        }

        // Camera rig: `origin` carries the pose (position = target,
        // eulerRotation = view yaw/pitch — camera nodes may use
        // eulerRotation; the object-transform ban doesn't apply).
        Node {
            id: cameraOrigin
            position: Qt.vector3d(0.1, 0.18, 0.05)
            eulerRotation.x: -38

            OrthographicCamera {
                id: camOrtho
                clipNear: 0.01
                clipFar: 100
                position: Qt.vector3d(0, 0, 3)
            }
            PerspectiveCamera {
                id: camPersp
                clipNear: 0.01
                clipFar: 100
                fieldOfView: 60
                position: Qt.vector3d(0, 0, 1.21)
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

        // Scene objects — the stable QAbstractListModel is the
        // primary source (granular transform updates keep delegates
        // alive through drags); the legacy objectList stays as the
        // probe fallback. rf() reads either shape.
        Node {
            id: sceneRoot

        Repeater3D {
            id: objRepeater
            model: (typeof bridge !== "undefined" && bridge.objectModel)
                   ? bridge.objectModel
                   : ((typeof bridge !== "undefined") ? bridge.objectList : [])

            // New model = new delegate set; drop stale node refs.
            onModelChanged: root.objNodes = ({})

            delegate: Node {
                id: objNode

                // Field reader: list-model role first, plain-map
                // modelData second (quickwidget_probe fallback).
                function rf(name, dflt) {
                    var v = model[name]
                    if (v === undefined && typeof modelData !== "undefined")
                        v = modelData[name]
                    return v === undefined ? dflt : v
                }

                property string oId:     rf("id", "")
                property string oGeom:   rf("geometry", "")
                property string oParent: rf("parentId", "")
                property string oInst:   rf("instancePath", oId)
                property string oKind:   rf("kind", "decor")
                property string oBound:  rf("bound", "none")
                property real   oBx:     rf("bx", 0)
                property real   oBy:     rf("by", 0)
                property real   oBz:     rf("bz", 0)
                property var    spec:    root.bodySpec(oGeom)

                // Structure (positions) and colors are split: the
                // Repeater's model is emitterPos, which only changes
                // on a scene/layout change, so delegates persist.
                // Per-tick updates only reassign emitterColors.
                property var emitterPos: []
                property var emitterColors: []

                function reloadLayout() {
                    var list = (typeof bridge !== "undefined") ? bridge.emittersOf(oId) : []
                    emitterPos = list
                    var cols = []
                    for (var i = 0; i < list.length; i++) cols.push(list[i].c)
                    emitterColors = cols
                }

                function reloadColors() {
                    emitterColors = (typeof bridge !== "undefined") ? bridge.emitterColorsOf(oId) : []
                }

                position: Qt.vector3d(rf("x", 0), rf("y", 0), rf("z", 0))
                // One shared rotation convention: the core converts
                // the stored XYZ degrees (Rz*Ry*Rx) into this
                // quaternion — eulerRotation here would silently
                // apply ZXY instead.
                rotation: Qt.quaternion(rf("qw", 1), rf("qx", 0),
                                        rf("qy", 0), rf("qz", 0))
                scale: Qt.vector3d(rf("sx", 1), rf("sy", 1), rf("sz", 1))
                visible: rf("visible", true)

                Component.onCompleted: {
                    root.objNodes[oId] = objNode
                    var p = root.objNodes[oParent]
                    objNode.parent = p ? p : sceneRoot
                    reloadLayout()
                    // Last delegate done — repair any parent that was
                    // created after its child.
                    if (Object.keys(root.objNodes).length === objRepeater.count)
                        root.fixupParents()
                }
                Component.onDestruction: {
                    if (root.objNodes[oId] === objNode)
                        delete root.objNodes[oId]
                }

                Connections {
                    target: (typeof bridge !== "undefined") ? bridge : null
                    function onEmittersChanged(changedId) {
                        if (changedId === objNode.oId) objNode.reloadColors()
                    }
                    function onSceneChanged() { objNode.reloadLayout() }
                }

                // Body mesh — ghost shells (the case) are translucent
                // containers; leaving them pickable would swallow
                // every pick aimed at hardware inside them.
                Model {
                    objectName: "obj|" + objNode.oId
                    pickable: !(objNode.spec && objNode.spec.ghost)
                    visible: objNode.spec !== null
                    source: objNode.spec ? objNode.spec.src : "#Cube"
                    scale: objNode.spec
                           ? Qt.vector3d(objNode.oBx / 100,
                                         objNode.oBy / 100,
                                         objNode.oBz / 100)
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

                        // Visible dot — not pickable itself (too
                        // small); the proxy below handles picking.
                        Model {
                            source: "#Sphere"
                            position: Qt.vector3d(modelData.x, modelData.y, modelData.z)
                            property real d: root.emitterSize(objNode.oGeom) / 100
                            scale: Qt.vector3d(d, d, d)
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                property color c: (modelData.i < objNode.emitterColors.length)
                                                  ? objNode.emitterColors[modelData.i] : "#000000"
                                baseColor: (objNode.oBound === "ok" || objNode.oBound === "none")
                                           ? c : Qt.darker(c, 2.5)
                            }
                        }

                        // Invisible pick proxy ~2.6x the dot so LEDs
                        // are actually hittable with a mouse.
                        Model {
                            objectName: "emit|" + objNode.oId + "|" + modelData.i
                            pickable: true
                            source: "#Sphere"
                            position: Qt.vector3d(modelData.x, modelData.y, modelData.z)
                            property real pd: root.emitterSize(objNode.oGeom) * 2.6 / 100
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
                    visible: (typeof bridge !== "undefined") && bridge.selectedId === objNode.oId
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

        // Move-tool pivot marker: 3D axis cross at the shared
        // selection pivot (rotate mode's handle is the 2D ring on
        // the gizmo overlay).
        Node {
            id: pivotMarker
            visible: gizmo.hasPivot && selCtl.tool === 0 && selCtl.gesture === 0
            position: gizmo.pivotWorld ? gizmo.pivotWorld : Qt.vector3d(0, 0, 0)
            Model {
                source: "#Cube"; position: Qt.vector3d(0.035, 0, 0)
                scale: Qt.vector3d(0.0007, 0.00008, 0.00008)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#e05050"
                }
            }
            Model {
                source: "#Cube"; position: Qt.vector3d(0, 0.035, 0)
                scale: Qt.vector3d(0.00008, 0.0007, 0.00008)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#50c050"
                }
            }
            Model {
                source: "#Cube"; position: Qt.vector3d(0, 0, 0.035)
                scale: Qt.vector3d(0.00008, 0.00008, 0.0007)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#5080e0"
                }
            }
            Model {
                source: "#Sphere"
                scale: Qt.vector3d(0.00012, 0.00012, 0.00012)
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#ffd040"
                }
            }
        }
        }

        /*-----------------------------------------------------*\
        || Editor controllers                                   ||
        \*-----------------------------------------------------*/
        Ed.CameraController {
            id: camCtl
            view3d: view
            origin: cameraOrigin
            orthoCam: camOrtho
            perspCam: camPersp
            boundsOf: root.instanceWorldBounds
            onPoseFinished: {
                if (typeof bridge !== "undefined")
                    bridge.setCameraState(stateMap())
            }
        }

        Ed.SelectionController {
            id: selCtl
            view3d: view
            cam: camCtl
            gizmo: gizmo
            eachNode: root.eachNode
        }

        Ed.TransformGizmo {
            id: gizmo
            anchors.fill: parent
            view3d: view
            cam: camCtl
            ctl: selCtl
            eachNode: root.eachNode
        }

        /* Single input router — every pointer press/drag/release
           and wheel goes through selCtl. Middle-button pan wins
           over any surface; see SelectionController.qml. */
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            onPressed: function(m) {
                root.forceActiveFocus()
                selCtl.press(m.x, m.y, m.button, m.modifiers)
            }
            onPositionChanged: function(m) {
                if (m.buttons === 0)
                    return
                selCtl.dragTo(m.x, m.y, m.modifiers)
            }
            onReleased: function(m) {
                selCtl.endGesture(m.x, m.y, m.button, m.modifiers)
            }
            onCanceled: selCtl.cancel()
            onWheel: function(w) {
                selCtl.wheelAt(w.x, w.y, w.angleDelta.y)
                w.accepted = true
            }
        }

        /* Marquee rubber band. */
        Rectangle {
            visible: selCtl.gesture === 4
            x: selCtl.marqueeRect.x
            y: selCtl.marqueeRect.y
            width: selCtl.marqueeRect.width
            height: selCtl.marqueeRect.height
            color: "#206a9ad0"
            border.color: "#6a9ad0"
            border.width: 1
        }
    }

    /*-----------------------------------------------------*\
    || Editor panel + hints                                 ||
    \*-----------------------------------------------------*/
    Ed.TransformInspector {
        id: inspector
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        ctl: selCtl
        cam: camCtl
    }

    Text {
        id: pickedLabel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        color: "#d8d8e0"
        font.pixelSize: 13
        text: (typeof bridge !== "undefined" && bridge.selectedId !== "")
              ? "selected: " + bridge.selectedId
              : "click: select · drag: move/marquee · MMB/Space+drag: pan · wheel: zoom · W/E/P tools · F/Home: frame"
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

    /*-----------------------------------------------------*\
    || Keyboard — shortcuts never fire while a text field   ||
    || has focus (inspector.textFocus). Escape is global:   ||
    || it always cancels the live gesture.                  ||
    \*-----------------------------------------------------*/
    Keys.onPressed: function(e) {
        if (e.key === Qt.Key_Space && !e.isAutoRepeat) {
            selCtl.spaceDown = true
            e.accepted = true
        }
    }
    Keys.onReleased: function(e) {
        if (e.key === Qt.Key_Space) {
            selCtl.spaceDown = false
            e.accepted = true
        }
    }

    Shortcut { sequence: "W";    enabled: !inspector.textFocus
               onActivated: selCtl.setTool(0) }
    Shortcut { sequence: "E";    enabled: !inspector.textFocus
               onActivated: selCtl.setTool(1) }
    Shortcut { sequence: "P";    enabled: !inspector.textFocus
               onActivated: selCtl.setTool(2) }
    /* Frame commands emit poseFinished under a live gesture's camera
       snapshot — gate them on the press window the same way. */
    Shortcut { sequence: "F";    enabled: !inspector.textFocus
               onActivated: if (!root.gestureBusy()) selCtl.frameSelection() }
    Shortcut { sequence: "Home"; enabled: !inspector.textFocus
               onActivated: if (!root.gestureBusy()) selCtl.frameAll() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !inspector.textFocus
               onActivated: root.undoOrRedo(false) }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: !inspector.textFocus
               onActivated: root.undoOrRedo(true) }
    Shortcut { sequence: "Ctrl+Y"; enabled: !inspector.textFocus
               onActivated: root.undoOrRedo(true) }
    Shortcut { sequence: "Escape"
               onActivated: root.escapeAll() }

    Connections {
        target: (typeof bridge !== "undefined") ? bridge : null
        function onCameraChanged() { camCtl.applyFromBridge() }
    }

    /* Focus loss mid-space-hold would leave a sticky pan modifier —
       the next left-drag would unexpectedly pan. spaceDown mirrors
       the physical key, so it is cleared ONLY here (app deactivation)
       and on key release — never on gesture end.
       This must key off Qt.application.state, not Window.active:
       under QQuickWidget the offscreen QQuickWindow never becomes
       active, so activeChanged never fires — application state is
       app-global and does. The cancel() also drops any gesture whose
       grab deactivation ate (pressed/deferPose can't stick). */
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                selCtl.spaceDown = false
                selCtl.cancel()
            }
        }
    }

    Component.onCompleted: camCtl.applyFromBridge()
}
