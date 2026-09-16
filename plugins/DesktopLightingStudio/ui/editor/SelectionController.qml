/*---------------------------------------------------------*\
|| SelectionController.qml                                 ||
||                                                         ||
||   The ONE input router for the desk viewport. Every    ||
||   pointer press lands in press() -> beginPressAt();    ||
||   drags promote past a ~4 px threshold into exactly    ||
||   one gesture:                                         ||
||     middle drag            -> camera pan (wins over    ||
||                               everything, any surface) ||
||     space + left drag      -> camera pan (alternative) ||
||     alt + left drag        -> orbit (free view only)   ||
||     left drag on device    -> move / rotate (by tool)  ||
||     left drag on ring      -> rotate (rotate tool)     ||
||     left drag on empty     -> marquee                  ||
||     left drag in Paint     -> emitter painting only    ||
||   A press released under the threshold is a click:     ||
||   select, ctrl-additive toggle, or empty-space clear.  ||
||   Escape -> cancel(): transform gestures restore the   ||
||   begin snapshot via bridge.cancelTransformGesture,    ||
||   pan/orbit restore the pose snapshot, marquee just    ||
||   clears.                                              ||
||                                                         ||
||   Test seam: beginPressAt/dragTo/endGesture take       ||
||   explicit hit info, so the QTest suite can drive the  ||
||   same routing without synthesizing real mouse events. ||
\*---------------------------------------------------------*/
import QtQuick

Item {
    id: ctl
    objectName: "selCtl"

    property var view3d: null
    property var cam: null          /* CameraController        */
    property var gizmo: null        /* TransformGizmo          */
    /* eachNode(cb) — iterate the live object-node map. */
    property var eachNode: null

    property int  tool: 0           /* 0 move, 1 rotate, 2 paint */
    property int  editPlane: 1      /* 0 free 1 deskXZ 2 frontXY 3 sideYZ */
    property bool snapEnabled: false
    property bool spaceDown: false
    property real clickPx: 4.0

    /* Gesture ids (mirrored as ints for the C++ test). */
    readonly property int gNone:    0
    readonly property int gPan:     1
    readonly property int gMove:    2
    readonly property int gRotate:  3
    readonly property int gMarquee: 4
    readonly property int gOrbit:   5
    readonly property int gPaint:   6

    property int    gesture: gNone
    property bool   pressed: false
    property point  pressPos: Qt.point(0, 0)
    property point  lastPos: Qt.point(0, 0)
    property int    pressButton: 0
    property int    pressMods: 0
    property string pressHit: ""     /* objectName hit at press, "" empty */
    property string hitObject: ""    /* object id owning pressHit        */
    property var    startHit: null   /* world-space press point          */
    property var    camSnapshot: null
    property int    planeAxis: 1
    property real   planeValue: 0
    property real   startAngle: 0
    property rect   marqueeRect: Qt.rect(0, 0, 0, 0)
    property string lastPainted: ""

    /* Test seam: the real scene uses the `bridge` context property;
       the qmltestrunner suite (no C++ context) injects a stub here.
       `bridge` is an unqualified name — referencing it when absent
       throws ReferenceError, so the fallback lives behind try/catch. */
    property var bridgeOverride: null

    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function hasBridge() { return br() !== null }

    function ownerOf(hitName) {
        if (!hitName)
            return ""
        if (hitName.indexOf("emit|") === 0)
            return hitName.split("|")[1]
        if (hitName.indexOf("obj|") === 0)
            return hitName.substring(4)
        return ""
    }

    function instOf(objectId) {
        var s = objectId.indexOf("/")
        return s < 0 ? objectId : objectId.substring(0, s)
    }

    function snapNow(mods) {
        return snapEnabled || ((mods & Qt.ShiftModifier) !== 0)
    }

    function pickPos(r) {
        if (r.scenePosition !== undefined)
            return r.scenePosition
        if (r.position !== undefined)
            return r.position
        return null
    }

    function normRect(a, b) {
        var x0 = Math.min(a.x, b.x), y0 = Math.min(a.y, b.y)
        return Qt.rect(x0, y0, Math.abs(a.x - b.x), Math.abs(a.y - b.y))
    }

    /*-----------------------------------------------------*\
    || Press — real input picks first; the test suite calls ||
    || beginPressAt directly with the resolved hit.         ||
    \*-----------------------------------------------------*/
    function press(x, y, button, mods) {
        var name = "", pt = null
        if (view3d) {
            var r = view3d.pick(x, y)
            if (r && r.objectHit) {
                name = r.objectHit.objectName
                pt   = pickPos(r)
            }
        }
        beginPressAt(x, y, button, mods, name, pt)
    }

    function beginPressAt(x, y, button, mods, hitName, hitPos) {
        /* A second press mid-gesture cancels the live one — a release
           here would commit a half-finished move. */
        if (pressed)
            cancel()
        pressed     = true
        gesture     = gNone
        pressPos    = Qt.point(x, y)
        lastPos     = pressPos
        pressButton = button
        pressMods   = mods
        pressHit    = hitName ? hitName : ""
        hitObject   = ownerOf(pressHit)
        startHit    = hitPos
        camSnapshot = cam ? cam.poseSnapshot() : null
        lastPainted = ""
        marqueeRect = Qt.rect(0, 0, 0, 0)
        /* Defer pose persistence for the WHOLE press window, not just
           promoted pan/orbit: a wheel inside the click-candidate
           window must not write a pose a later Escape would roll
           back. Cleared in reset(). */
        if (cam) cam.deferPose = true

        /* Middle pan wins over every surface and every tool. The
           middle_drag controls pref can redirect it to orbit — only
           meaningful in the perspective free view, so ortho presets
           keep panning (orbitPixels would no-op). Paint mode still
           forbids orbit: the pref degrades to pan there. */
        if (button === Qt.MiddleButton) {
            var md = (hasBridge() && br().cameraState)
                   ? br().cameraState.middle_drag : "pan"
            gesture = (md === "orbit" && tool !== 2 && cam && !cam.ortho)
                    ? gOrbit : gPan
            return
        }
        if (button !== Qt.LeftButton)
            return

        if (tool === 2) {
            /* Paint mode: pointer gestures can never start movement
               or camera orbit — only emitter painting. */
            if (pressHit.indexOf("emit|") === 0) {
                paintHit(pressHit)
                gesture = gPaint
            }
            return
        }
        /* Move/Rotate: stay a click candidate until dragTo() trips
           the threshold. */
    }

    /*-----------------------------------------------------*\
    || Drag                                                 ||
    \*-----------------------------------------------------*/
    function rotateAxis() {
        if (editPlane === 1)
            return Qt.vector3d(0, 1, 0)
        if (editPlane === 2)
            return Qt.vector3d(0, 0, 1)
        if (editPlane === 3)
            return Qt.vector3d(1, 0, 0)
        return cam && cam.origin ? cam.origin.forward : Qt.vector3d(0, 1, 0)
    }

    function beginTransform() {
        if (!hasBridge())
            return false
        var b = br()
        b.beginTransformGesture()
        if (b.gestureActive && !b.gestureActive())
            return false
        if (!startHit)
            startHit = cam ? cam.pointUnderPointer(pressPos.x, pressPos.y) : null
        if (editPlane === 1)      { planeAxis = 1; planeValue = startHit ? startHit.y : 0 }
        else if (editPlane === 2) { planeAxis = 2; planeValue = startHit ? startHit.z : 0 }
        else if (editPlane === 3) { planeAxis = 0; planeValue = startHit ? startHit.x : 0 }
        else                      { planeAxis = -1 }
        return true
    }

    function planePoint(x, y) {
        if (!cam)
            return null
        if (planeAxis < 0) {
            if (!startHit)
                return null
            return cam.planeHitPoint(x, y, -1, startHit)
        }
        return cam.planeHitPoint(x, y, planeAxis, planeValue)
    }

    function ensureHitSelected(mods) {
        if (!hasBridge() || hitObject === "")
            return
        var inst = instOf(hitObject)
        if (br().selectedInstances.indexOf(inst) >= 0)
            return
        if (mods & Qt.ControlModifier)
            br().selectInstance(inst, true)
        else
            br().select(hitObject)
    }

    function startRotate() {
        if (gizmo)
            gizmo.updatePivot()
        startAngle = gizmo ? gizmo.angleAt(pressPos.x, pressPos.y) : 0
    }

    function dragTo(x, y, mods) {
        if (!pressed)
            return
        var dx = x - lastPos.x
        var dy = y - lastPos.y
        lastPos = Qt.point(x, y)

        if (gesture === gPan) {
            if (cam) cam.panPixels(dx, dy)
            return
        }
        if (gesture === gOrbit) {
            if (cam) cam.orbitPixels(dx, dy)
            return
        }
        if (gesture === gPaint) {
            if (view3d) {
                var rp = view3d.pick(x, y)
                if (rp && rp.objectHit
                    && rp.objectHit.objectName.indexOf("emit|") === 0)
                    paintHit(rp.objectHit.objectName)
            }
            return
        }
        if (gesture === gMove) {
            var hp = planePoint(x, y)
            if (hp && startHit && hasBridge()) {
                var d = hp.minus(startHit)
                br().updateTransformGesture(d.x, d.y, d.z,
                                            editPlane, snapNow(mods))
            }
            return
        }
        if (gesture === gRotate) {
            var a  = gizmo ? gizmo.angleAt(x, y) : 0
            var ax = rotateAxis()
            if (hasBridge())
                br().updateRotateGestureAxis(ax.x, ax.y, ax.z,
                                             a - startAngle,
                                             snapNow(mods))
            return
        }
        if (gesture === gMarquee) {
            marqueeRect = normRect(pressPos, lastPos)
            return
        }

        /* Still a click candidate — the threshold decides. */
        if (pressButton !== Qt.LeftButton)
            return
        var mx = x - pressPos.x
        var my = y - pressPos.y
        if (Math.sqrt(mx * mx + my * my) < clickPx)
            return

        /* Paint mode: a left-drag that started outside an emit| proxy
           can NEVER promote into movement, orbit or marquee — pointer
           gestures in Paint do nothing but paint. */
        if (tool === 2)
            return

        /* Gesture promotion, in contract order. deferPose is already
           armed from beginPressAt. */
        if (spaceDown) {
            gesture = gPan
            if (cam) cam.panPixels(mx, my)
            return
        }
        if ((mods & Qt.AltModifier) && cam && !cam.ortho) {
            gesture = gOrbit
            cam.orbitPixels(mx, my)
            return
        }
        if (tool === 1 && gizmo && gizmo.ringHit(pressPos.x, pressPos.y)
            && hasBridge() && br().selectedInstances.length > 0) {
            if (beginTransform()) {
                startRotate()
                gesture = gRotate
            }
            return
        }
        if (hitObject !== "") {
            ensureHitSelected(mods)
            if (beginTransform()) {
                if (tool === 1) {
                    startRotate()
                    gesture = gRotate
                } else {
                    gesture = gMove
                }
            }
            return
        }
        gesture = gMarquee
        marqueeRect = normRect(pressPos, lastPos)
    }

    /*-----------------------------------------------------*\
    || Release / cancel                                     ||
    \*-----------------------------------------------------*/
    function endGesture(x, y, button, mods) {
        if (!pressed)
            return
        /* Only the button that STARTED the press ends it — a release
           of a different button (e.g. letting go of left while a
           middle-pan is still held) must not commit/reset early. */
        if (button !== pressButton)
            return
        lastPos = Qt.point(x, y)
        var g = gesture
        if (g === gMove || g === gRotate) {
            if (hasBridge())
                br().commitTransformGesture()
        } else if (g === gMarquee) {
            var r = normRect(pressPos, lastPos)
            selectMarquee(r.x, r.y, r.x + r.width, r.y + r.height)
        } else if (g === gPan || g === gOrbit) {
            if (cam)
                cam.endPose()     /* persist final pose */
        } else if (g === gNone && button === Qt.LeftButton
                   && pressButton === Qt.LeftButton) {
            clickAt(mods)
        }
        /* A wheel zoom deferred during the press window persists
           once here (pan/orbit already saved via endPose above —
           its deferredSave flag is cleared there). */
        flushCamPose()
        reset()
    }

    function clickAt(mods) {
        if (!hasBridge())
            return
        if (hitObject === "") {
            br().select("")                /* empty space clears */
            return
        }
        if (mods & Qt.ControlModifier)
            br().selectInstance(instOf(hitObject), true)
        else
            br().select(hitObject)
    }

    /* Escape — restore pre-gesture state, zero history. */
    function cancel() {
        var g = gesture
        var wasPressed = pressed
        if (g === gMove || g === gRotate) {
            if (hasBridge())
                br().cancelTransformGesture()
            flushCamPose()        /* camera pose isn't restored — a
                                     deferred wheel zoom still saves */
        } else if (g === gPan || g === gOrbit) {
            if (cam)
                cam.restorePose(camSnapshot)   /* no save; the snapshot
                                                  wins over any
                                                  deferred zoom */
        } else {
            flushCamPose()        /* gNone/gPaint/gMarquee — nothing
                                     camera-side to restore */
        }
        reset()
        return wasPressed || g !== gNone
    }

    /* Persist a wheel-zoom pose deferred during the press window —
       no-op when the camera doesn't track one (test stubs). */
    function flushCamPose() {
        if (cam && typeof cam.flushDeferredPose === "function")
            cam.flushDeferredPose()
    }

    function reset() {
        gesture     = gNone
        pressed     = false
        /* spaceDown is NOT cleared here — it mirrors the physical
           Space key and is synced only by key press/release and
           application deactivation (StudioScene). Clearing it on
           gesture end would desync from a still-held Space (held-key
           autorepeat is filtered out, so nothing would re-arm it) and
           misroute the next left-drag to move/marquee. */
        pressHit    = ""
        hitObject   = ""
        startHit    = null
        marqueeRect = Qt.rect(0, 0, 0, 0)
        lastPainted = ""
        if (cam) { cam.deferPose = false; cam.deferredSave = false }
    }

    /*-----------------------------------------------------*\
    || Non-drag entry points                                ||
    \*-----------------------------------------------------*/
    function wheelAt(x, y, angleDelta) {
        if (cam)
            cam.zoomAt(x, y, angleDelta)
    }

    function setTool(t) {
        if (t === tool)
            return
        cancel()
        tool = t
    }

    function paintHit(name) {
        if (!hasBridge() || name === lastPainted)
            return
        var p = name.split("|")
        if (p.length < 3)
            return
        lastPainted = name
        br().paintEmitter(p[1], parseInt(p[2]), br().paintColor)
    }

    /* Marquee: project every non-decor node and keep the enclosed
       instance ids. */
    function selectMarquee(x0, y0, x1, y1) {
        if (!hasBridge() || !view3d || !eachNode)
            return
        var lo = Math.min(x0, x1), hi = Math.max(x0, x1)
        var to = Math.min(y0, y1), bo = Math.max(y0, y1)
        var ids = []
        eachNode(function(item) {
            if (!item || !item.visible || item.oKind === "decor")
                return
            var p = view3d.mapFrom3DScene(item.scenePosition)
            if (p.x >= lo && p.x <= hi && p.y >= to && p.y <= bo
                && ids.indexOf(item.oInst) < 0)
                ids.push(item.oInst)
        })
        br().clearEditorSelection()
        for (var k = 0; k < ids.length; k++)
            br().selectInstance(ids[k], true)
    }

    function frameSelection() {
        if (hasBridge() && cam && br().selectedInstances.length > 0)
            cam.frameIds(br().selectedInstances)
    }

    function frameAll() {
        if (cam)
            cam.frameIds(null)
    }
}
