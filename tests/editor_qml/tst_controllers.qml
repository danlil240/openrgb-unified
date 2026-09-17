/*---------------------------------------------------------*\
||  tst_controllers.qml                                    ||
||                                                         ||
||  Pure-QML gesture/camera tests run by the SIGNED Qt     ||
||  qmltestrunner.exe — the only test path that survives   ||
||  Smart App Control, which blocks every freshly built    ||
||  unsigned exe (the C++ editor_qml_test stays as the     ||
||  full end-to-end harness for machines where it runs).   ||
||                                                         ||
||  SelectionController / CameraController are instantiated||
||  with stub view/cam/bridge objects (bridge via the      ||
||  bridgeOverride test seam). Assertions cover the M2.2   ||
||  fix-round contract: Paint-mode exclusivity, gesture    ||
||  cancel-on-repress, spaceDown lifecycle, middle_drag    ||
||  pref, marquee exclusion, deferred pose persistence.    ||
\*---------------------------------------------------------*/
import QtQuick
import QtTest
import "../../plugins/DesktopLightingStudio/ui" as Ui
import "../../plugins/DesktopLightingStudio/ui/editor" as Ed

TestCase {
    id: tc
    name: "EditorControllers"
    when: windowShown

    /*---------------- stubs ----------------*/
    function makeBridge() {
        return {
            cameraState: { "view": "desk", "projection": "orthographic",
                           "tx": 0.1, "ty": 0.18, "tz": 0.05,
                           "yaw": 0, "pitch": -38,
                           "distance": 1.21, "span": 0.9,
                           "middle_drag": "pan" },
            selectedInstances: [],
            paintColor: "#ff0000",
            log: [],
            select: function(o) { this.log.push("select:" + o) },
            selectInstance: function(i, add) {
                this.log.push("selectInstance:" + i)
                if (this.selectedInstances.indexOf(i) < 0)
                    this.selectedInstances.push(i)
            },
            clearEditorSelection: function() {
                this.log.push("clearSel"); this.selectedInstances = []
            },
            beginTransformGesture: function() { this.log.push("beginT") },
            gestureActive: function() { return true },
            updateTransformGesture: function(dx, dy, dz, pl, sn) {
                this.log.push("updT:" + dx.toFixed(3) + "," + dz.toFixed(3))
            },
            updateRotateGestureAxis: function(x, y, z, a, sn) {
                this.log.push("updR:" + a.toFixed(2))
            },
            commitTransformGesture: function() { this.log.push("commitT") },
            cancelTransformGesture: function() { this.log.push("cancelT") },
            paintEmitter: function(inst, idx, c) {
                this.log.push("paint:" + inst + "|" + idx)
            }
        }
    }

    function makeCam() {
        return {
            ortho: true, deferPose: false, deferredSave: false, log: [],
            origin: { forward: Qt.vector3d(0, 0, -1) },
            poseSnapshot: function() {
                return { t: Qt.vector3d(0, 0, 0), yaw: 0, pitch: -38,
                         dist: 1.21, sp: 0.9, vn: "desk" }
            },
            panPixels: function(dx, dy) { this.log.push("pan") },
            orbitPixels: function(dx, dy) { this.log.push("orbit") },
            endPose: function() { this.deferredSave = false
                                  this.log.push("endPose") },
            flushDeferredPose: function() {
                if (this.deferredSave) {
                    this.deferredSave = false
                    this.log.push("flush")
                }
            },
            restorePose: function(s) { this.log.push("restore") },
            pointUnderPointer: function(x, y) {
                return Qt.vector3d(0.1, 0.02, 0.2)
            },
            planeHitPoint: function(x, y, ax, v) {
                return Qt.vector3d(0.3, 0.02, 0.4)
            },
            zoomAt: function(x, y, a) { this.log.push("zoom") },
            frameIds: function(ids) { this.log.push("frame") }
        }
    }

    function makeView() {
        return {
            nextPick: null,
            pick: function(x, y) { return this.nextPick },
            mapFrom3DScene: function(p) { return this._map(p) },
            _map: function(p) { return Qt.vector3d(0, 0, 0) },
            mapTo3DScene: function(v) {
                return Qt.vector3d((v.x - 250) * 0.002,
                                   (250 - v.y) * 0.002, 1.0 - v.z)
            },
            height: 500, camera: null
        }
    }

    function objPick(name) {
        return { objectHit: { objectName: name },
                 scenePosition: Qt.vector3d(0.1, 0.02, 0.2) }
    }

    property var sel: null
    property var brStub: null
    property var camStub: null
    property var viewStub: null
    property var gizStub: null
    property var camCtl: null

    Component { id: selComp; Ed.SelectionController {} }
    Component { id: camComp; Ed.CameraController {} }

    function init() {
        sel = selComp.createObject(tc)
        brStub = makeBridge()
        camStub = makeCam()
        viewStub = makeView()
        gizStub = { ring: false,
                    updatePivot: function() {},
                    angleAt: function(x, y) { return x * 0.01 },
                    ringHit: function(x, y) { return this.ring } }
        sel.bridgeOverride = brStub
        sel.cam = camStub
        sel.view3d = viewStub
        sel.gizmo = gizStub
        sel.eachNode = function(cb) {}
        sel.tool = 0
    }

    function cleanup() {
        if (sel) { sel.destroy(); sel = null }
        if (camCtl) { camCtl.destroy(); camCtl = null }
    }

    function logged(log, prefix) {
        return log.filter(function(s) { return s.indexOf(prefix) === 0 })
    }

    /*---------------- SelectionController ----------------*/

    /* C1: Paint mode — a left drag on a device body must never
       promote to move/orbit/marquee: no transform calls, no camera
       calls. */
    function test_paint_drag_never_moves() {
        sel.tool = 2
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 140, 0)
        sel.dragTo(200, 180, 0)
        compare(sel.gesture, 0, "paint-mode drag promoted a gesture")
        compare(logged(brStub.log, "beginT").length, 0)
        compare(logged(brStub.log, "updT").length, 0)
        compare(logged(camStub.log, "pan").length, 0)
        compare(logged(camStub.log, "orbit").length, 0)
        sel.endGesture(200, 180, Qt.LeftButton, 0)
        compare(logged(brStub.log, "commitT").length, 0)
        compare(sel.pressed, false)
    }

    /* C1b: Alt held does not unlock orbit in Paint mode. */
    function test_paint_alt_drag_no_orbit() {
        camStub.ortho = false
        sel.tool = 2
        sel.beginPressAt(100, 100, Qt.LeftButton, Qt.AltModifier,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 100, Qt.AltModifier)
        compare(sel.gesture, 0)
        compare(logged(camStub.log, "orbit").length, 0)
        sel.endGesture(160, 100, Qt.LeftButton, Qt.AltModifier)
    }

    /* C1c: empty-space drag in Paint mode never becomes marquee. */
    function test_paint_empty_drag_no_marquee() {
        sel.tool = 2
        sel.beginPressAt(100, 100, Qt.LeftButton, 0, "", null)
        sel.dragTo(300, 300, 0)
        compare(sel.gesture, 0)
        sel.endGesture(300, 300, Qt.LeftButton, 0)
        compare(logged(brStub.log, "selectInstance").length, 0)
        compare(logged(brStub.log, "clearSel").length, 0)
    }

    /* Paint drags keep painting emit| proxies. */
    function test_paint_emitter_drag_paints() {
        sel.tool = 2
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "emit|kbd|3", Qt.vector3d(0, 0.02, 0.2))
        compare(sel.gesture, 6)
        viewStub.nextPick = objPick("emit|kbd|5")
        sel.dragTo(120, 100, 0)
        viewStub.nextPick = objPick("obj|kbd")
        sel.dragTo(140, 100, 0)
        sel.endGesture(140, 100, Qt.LeftButton, 0)
        var paints = logged(brStub.log, "paint:")
        compare(paints.length, 2)
        compare(paints[0], "paint:kbd|3")
        compare(paints[1], "paint:kbd|5")
    }

    /* Baseline: Move tool still moves on a device drag. */
    function test_move_drag_moves() {
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 100, 0)        /* promotes past threshold */
        compare(sel.gesture, 2)
        compare(logged(brStub.log, "beginT").length, 1)
        sel.dragTo(170, 100, 0)        /* first post-promotion update */
        verify(logged(brStub.log, "updT").length > 0)
        sel.endGesture(160, 100, Qt.LeftButton, 0)
        compare(logged(brStub.log, "commitT").length, 1)
    }

    /* M2: a second press mid-gesture cancels — never commits a
       half-finished move. */
    function test_second_press_cancels() {
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 100, 0)
        compare(sel.gesture, 2)
        sel.beginPressAt(300, 100, Qt.LeftButton, 0, "", null)
        compare(sel.gesture, 0)
        compare(logged(brStub.log, "cancelT").length, 1)
        compare(logged(brStub.log, "commitT").length, 0)
        sel.endGesture(300, 100, Qt.LeftButton, 0)
        compare(logged(brStub.log, "commitT").length, 0)
    }

    /* D1: spaceDown tracks the physical key — gesture end and
       cancel() must NOT clear it (only key release / application
       deactivation do, in the host scene). Clearing it on reset()
       desynced from a still-held Space and, since held-key
       autorepeat is filtered, misrouted every second held-space
       drag to move/marquee. */
    function test_spaceDown_survives_gesture_end() {
        sel.spaceDown = true
        sel.cancel()
        compare(sel.spaceDown, true, "cancel desynced spaceDown")
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 100, 0)
        compare(sel.gesture, 1)          /* space+drag pans */
        sel.endGesture(160, 100, Qt.LeftButton, 0)
        compare(sel.spaceDown, true, "gesture end desynced spaceDown")
        compare(logged(camStub.log, "pan").length, 1)
        compare(logged(camStub.log, "endPose").length, 1)
        /* Second held-space drag must pan too — the regression. */
        sel.beginPressAt(200, 200, Qt.LeftButton, 0, "", null)
        sel.dragTo(260, 200, 0)
        compare(sel.gesture, 1, "second held-space drag did not pan")
        sel.endGesture(260, 200, Qt.LeftButton, 0)
        sel.spaceDown = false
    }

    /* D4: deferPose is armed at PRESS for every gesture-capable
       button — a wheel inside the click-candidate window can't
       persist a pose a later Escape would roll back. */
    function test_defer_pose_at_press() {
        sel.beginPressAt(100, 100, Qt.LeftButton, 0, "", null)
        compare(camStub.deferPose, true,
                "click-candidate press did not defer pose saves")
        sel.endGesture(100, 100, Qt.LeftButton, 0)
        compare(camStub.deferPose, false)
    }

    /* D4b (real CameraController): a wheel zoom deferred inside a
       non-camera gesture persists ONCE at gesture end — deferral is
       delay-to-release, not a drop — while Escape on a camera
       gesture drops it (the snapshot restore wins). */
    function test_deferred_zoom_flush_and_drop() {
        var c = makeRealCam()
        var saves = 0
        c.poseFinished.connect(function() { saves++ })
        sel.cam = c

        /* Wheel during a click-candidate -> move drag: deferred at
           zoom, flushed once at release. */
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        compare(c.deferPose, true)
        sel.wheelAt(100, 100, 120)
        compare(saves, 0, "zoom persisted inside the press window")
        compare(c.deferredSave, true)
        sel.dragTo(160, 100, 0)
        compare(sel.gesture, 2)
        sel.endGesture(160, 100, Qt.LeftButton, 0)
        compare(saves, 1, "deferred zoom not flushed at release")
        compare(c.deferPose, false)

        /* Wheel during a pan, then Escape: snapshot restore wins —
           the deferred save is dropped, never persisted. */
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0, "", null)
        compare(sel.gesture, 1)
        sel.wheelAt(100, 100, 120)
        compare(c.deferredSave, true)
        sel.cancel()
        compare(saves, 1, "deferred zoom persisted past a restore")
        compare(c.deferredSave, false)
    }

    /* D7: releasing a different button must not end the gesture —
       only the button that started the press does. */
    function test_cross_button_release_ignored() {
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0, "", null)
        compare(sel.gesture, 1)
        sel.dragTo(140, 100, 0)
        sel.endGesture(140, 100, Qt.LeftButton, 0)   /* wrong button */
        compare(sel.pressed, true, "foreign release ended the gesture")
        compare(logged(camStub.log, "endPose").length, 0)
        sel.endGesture(140, 100, Qt.MiddleButton, 0)
        compare(sel.pressed, false)
        compare(logged(camStub.log, "endPose").length, 1)
    }

    /* M4: middle_drag pref — "pan" pans, "orbit" orbits only in the
       perspective free view; ortho presets keep panning. */
    function test_middle_drag_pref() {
        /* default pan */
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0,
                         "obj|kbd", null)
        compare(sel.gesture, 1)
        sel.dragTo(140, 100, 0)
        compare(logged(camStub.log, "pan").length, 1)
        sel.endGesture(140, 100, Qt.MiddleButton, 0)
        compare(logged(camStub.log, "endPose").length, 1)

        /* orbit honored in free view */
        brStub.cameraState = { "middle_drag": "orbit" }
        camStub.ortho = false
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0,
                         "obj|kbd", null)
        compare(sel.gesture, 5)
        sel.dragTo(140, 100, 0)
        compare(logged(camStub.log, "orbit").length, 1)
        sel.endGesture(140, 100, Qt.MiddleButton, 0)
        compare(logged(camStub.log, "endPose").length, 2)

        /* orbit pref is meaningless under ortho presets -> pan */
        camStub.ortho = true
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0,
                         "obj|kbd", null)
        compare(sel.gesture, 1)
        sel.endGesture(100, 100, Qt.MiddleButton, 0)

        /* Paint mode forbids orbit even when the pref asks for it —
           degrades to pan. */
        camStub.ortho = false
        sel.tool = 2
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0,
                         "obj|kbd", null)
        compare(sel.gesture, 1)
        sel.endGesture(100, 100, Qt.MiddleButton, 0)
        sel.tool = 0
    }

    /* Middle pan on a device must never move the device. */
    function test_middle_pan_never_moves() {
        sel.beginPressAt(100, 100, Qt.MiddleButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 140, 0)
        compare(sel.gesture, 1)
        compare(logged(brStub.log, "beginT").length, 0)
        sel.endGesture(160, 140, Qt.MiddleButton, 0)
        compare(logged(brStub.log, "commitT").length, 0)
    }

    /* I1: marquee keeps enclosed instances and ONLY those. */
    function test_marquee_excludes_outside() {
        var nodes = [
            { visible: true, oKind: "device", oInst: "mouse",
              scenePosition: Qt.vector3d(0.0, 0.0, 0.0) },
            { visible: true, oKind: "device", oInst: "kbd",
              scenePosition: Qt.vector3d(5.0, 0.0, 0.0) }
        ]
        viewStub._map = function(p) {
            return p.x > 1.0 ? Qt.vector3d(400, 400, 0)
                             : Qt.vector3d(100, 100, 0)
        }
        sel.eachNode = function(cb) { for (var i = 0; i < nodes.length; i++) cb(nodes[i]) }
        sel.beginPressAt(50, 50, Qt.LeftButton, 0, "", null)
        sel.dragTo(200, 200, 0)
        compare(sel.gesture, 4)
        sel.endGesture(200, 200, Qt.LeftButton, 0)
        compare(brStub.selectedInstances.length, 1)
        compare(brStub.selectedInstances[0], "mouse")
    }

    /* Escape restores the transform snapshot; no commit. */
    function test_escape_restores() {
        sel.beginPressAt(100, 100, Qt.LeftButton, 0,
                         "obj|kbd", Qt.vector3d(0, 0.02, 0.2))
        sel.dragTo(160, 100, 0)
        compare(sel.gesture, 2)
        verify(sel.cancel())
        compare(logged(brStub.log, "cancelT").length, 1)
        compare(logged(brStub.log, "commitT").length, 0)
        compare(sel.gesture, 0)
    }

    /*---------------- CameraController -----------------*/

    function makeRealCam() {
        camCtl = camComp.createObject(tc)
        camCtl.view3d = makeView()
        camCtl.origin = {
            position: Qt.vector3d(0, 0, 0),
            eulerRotation: Qt.vector3d(0, 0, 0),
            right: Qt.vector3d(1, 0, 0),
            up: Qt.vector3d(0, 1, 0),
            forward: Qt.vector3d(0, 0, -1)
        }
        camCtl.orthoCam = { horizontalMagnification: 0,
                            verticalMagnification: 0,
                            position: Qt.vector3d(0, 0, 0) }
        camCtl.perspCam = { position: Qt.vector3d(0, 0, 0),
                            fieldOfView: 60 }
        return camCtl
    }

    /* M1: wheel mid-gesture must not persist a half-gesture pose. */
    function test_wheel_pose_deferred() {
        var c = makeRealCam()
        var saves = 0
        c.poseFinished.connect(function() { saves++ })
        c.zoomAt(250, 250, 120)
        compare(saves, 1)
        c.deferPose = true
        c.zoomAt(250, 250, 120)
        c.zoomAt(250, 250, -120)
        compare(saves, 1, "zoom persisted mid-gesture")
        c.endPose()
        compare(saves, 2, "gesture end did not persist once")
    }

    /* M5: applyView("case") persists even with no case instance —
       the view change is still an editor pref. */
    function test_case_view_persists_without_instance() {
        var c = makeRealCam()
        var saves = 0
        c.poseFinished.connect(function() { saves++ })
        c.boundsOf = function(ids) { return null }
        c.applyView("case")
        compare(c.viewName, "case")
        compare(saves, 1)
    }

    function test_frameIds_persists() {
        var c = makeRealCam()
        var saves = 0
        c.poseFinished.connect(function() { saves++ })
        c.boundsOf = function(ids) {
            return { c: Qt.vector3d(0.2, 0.05, 0.3), r: 0.4 }
        }
        verify(c.frameIds(["kbd"]))
        compare(c.target.x, 0.2)
        compare(saves, 1)
    }

    /*---------------- 5.2 pick mode ------------------*/

    /* Review minor: Escape while a pick is ARMED but not pressed
       must disarm BOTH sides — pickMode AND pickTarget — and hand
       the owner a cancel so its armed UI (armedPick) clears. */
    function test_pick_escape_disarms() {
        var calls = []
        sel.pickMode = "origin"
        sel.pickPlaneY = 0.02
        sel.pickTarget = function(pt, phase) { calls.push(phase) }
        verify(sel.cancel(), "an armed pick consumes the Escape")
        compare(sel.pickMode, "")
        verify(sel.pickTarget === null, "pickTarget released")
        compare(calls.join(","), "cancel", "owner was told")
    }

    /* Mid-press Escape still routes the cancel through
       deliverPick before disarming. */
    function test_pick_midpress_escape() {
        var calls = []
        sel.pickMode = "origin"
        sel.pickPlaneY = 0.02
        sel.pickTarget = function(pt, phase) { calls.push(phase) }
        sel.beginPressAt(100, 100, Qt.LeftButton, 0, "", null)
        compare(sel.gesture, 7, "press in pick mode = gPick")
        compare(calls.join(","), "begin")
        sel.cancel()
        compare(sel.pickMode, "")
        verify(calls.join(",").indexOf("cancel") >= 0,
               "cancel delivered to the owner")
    }

    /* A lifecycle phase must land even when the ray misses the
       plane — dropping "cancel"/"end" would leak the owner's
       effect gesture (begin already ran). */
    function test_pick_cancel_without_plane_hit() {
        var calls = []
        camStub.planeHitPoint = function(x, y, ax, v) { return null }
        sel.pickMode = "origin"
        sel.pickTarget = function(pt, phase) {
            calls.push(phase + ":" + (pt === null))
        }
        sel.deliverPick(10, 10, "cancel")
        compare(calls.join(","), "cancel:true",
                "cancel landed with a null point")
        sel.deliverPick(10, 10, "begin")
        compare(calls.length, 1, "begin without a hit is dropped")
        sel.pickMode = ""
        sel.pickTarget = null
    }

    /*---------------- Compile smoke ------------------*/

    /* M9 + general: every edited QML file must compile, and
       StudioScene must instantiate without a `bridge` context. */
    function test_qml_files_compile() {
        var ui = "../../plugins/DesktopLightingStudio/ui/"
        var files = [ "StudioScene.qml",
                      "editor/CameraController.qml",
                      "editor/SelectionController.qml",
                      "editor/TransformGizmo.qml",
                      "editor/TransformInspector.qml" ]
        for (var i = 0; i < files.length; i++) {
            var c = Qt.createComponent(Qt.resolvedUrl(ui + files[i]))
            compare(c.status, Component.Ready,
                    files[i] + ": " + c.errorString())
        }
    }

    function test_studio_scene_instantiates() {
        var c = Qt.createComponent(Qt.resolvedUrl(
                    "../../plugins/DesktopLightingStudio/ui/StudioScene.qml"))
        compare(c.status, Component.Ready, c.errorString())
        var scene = c.createObject(tc)
        verify(scene !== null, "StudioScene failed to instantiate")
        scene.destroy()
    }
}
