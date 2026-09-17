/*---------------------------------------------------------*\
|||  tst_effects.qml                                        ||
|||                                                         ||
|||  Task 5.2 effect-layer editing coverage — pure QML    ||
|||  through the signed qmltestrunner (same harness as    ||
|||  tst_library.qml / tst_preset_editor.qml). All four   ||
|||  new components take the bridgeOverride seam; the     ||
|||  stub is a mini in-memory stack so mutations reflect  ||
|||  on re-read — same shape as the real bridge's         ||
|||  QVariantMap/invokable surface.                       ||
|||                                                         ||
|||  Asserts:                                               ||
|||    - components compile and instantiate               ||
|||    - LayerStack rows via the invokable fallback AND   ||
|||      the {count,rowAt} model shape; enable/move/       ||
|||      remove/add/blend route to the bridge; opacity    ||
|||      scrub = begin + moves + ONE commit               ||
|||    - srcBadge disconnected states (off / not ready)   ||
|||    - PaletteEditor stop ops + gap-midpoint addPos     ||
|||    - EffectInspector field visibility per primitive   ||
|||      (relevance table) + numeric/vec/target commits   ||
|||    - EffectEditor open/close, save-as routing,        ||
|||      viewport pick mode = one gesture per pick        ||
|||    - LookShelf needsBadge states                      ||
|||                                                         ||
|||  NOT covered: real QAbstractListModel delivery, real  ||
|||  View3D picking — both need the C++ harness; the      ||
|||  pick-mode contract is exercised through the          ||
|||  SelectionController function seam in the C++ suite.  ||
|\*--------------------------------------------------------*/
import QtQuick
import QtTest
import "../../plugins/DesktopLightingStudio/ui/components" as C

TestCase {
    id: tc
    name: "Effects"
    when: windowShown

    /*---------------- stubs ----------------*/

    /* Full-map layer rows — the shape bridge.effectLayer(i)
       returns (QVariantMap). */
    function mkLayers() {
        return [
            { index: 0, primitive: "static", enabled: true,
              blend: "replace", opacity: 1.0, space: "world",
              speed: 0, scale: 1, phase: 0, density: 1,
              source: "", seed: 0,
              origin: { x: 0, y: 0, z: 0 },
              direction: { x: 1, y: 0, z: 0 },
              path: [],
              palette: [ { pos: 0.0, color: "#102040" } ],
              targets: [] },
            { index: 1, primitive: "ripple", enabled: true,
              blend: "add", opacity: 0.8, space: "world",
              speed: 0.3, scale: 0.05, phase: 0, density: 1.5,
              source: "key", seed: 0,
              origin: { x: 0.1, y: 0.02, z: 0.2 },
              direction: { x: 0, y: 0, z: 0 },
              path: [],
              palette: [ { pos: 0.0, color: "#ff0000" },
                         { pos: 1.0, color: "#0000ff" } ],
              targets: [ "desk" ] },
            { index: 2, primitive: "comet", enabled: false,
              blend: "screen", opacity: 1.0, space: "world",
              speed: 0.5, scale: 0.3, phase: 0, density: 0,
              source: "", seed: 7,
              origin: { x: 0, y: 0, z: 0 },
              direction: { x: 0, y: 0, z: 0 },
              path: [ { x: 0, y: 0, z: 0 }, { x: 0.4, y: 0, z: 0.3 } ],
              palette: [ { pos: 0.0, color: "#ffffff" } ],
              targets: [] }
        ]
    }

    function rowOf(l, i) {
        return { index: i, primitive: l.primitive, enabled: l.enabled,
                 blend: l.blend, opacity: l.opacity, space: l.space,
                 source: l.source, stops: l.palette.length,
                 pathPts: l.path.length, summary: "" }
    }

    function makeBridge() {
        var b = {
            log: [],
            layers: mkLayers(),
            activePreset: "aurora",
            effectStackInline: true,
            gestureDepth: 0,
            saved: null,
            resetCalls: 0,
            inputStates: {
                audio:  { name: "audio",  enabled: true,  ready: true,
                          status: "listening" },
                key:    { name: "key",    enabled: false, ready: false,
                          status: "hook off" },
                screen: { name: "screen", enabled: true,  ready: false,
                          status: "capture unavailable" }
            },
            /* LookShelf sync fields */
            playing: false,
            effectSpeedPct: 100,
            effectIntensityPct: 100,
            brightnessPct: 100,
            audioInput: false, keyInput: false, screenInput: false,
            presetList: [
                { id: "aurora", name: "Aurora", needs: "",
                  fromFile: false, description: "" },
                { id: "keyripple", name: "Key Ripple", needs: "key",
                  fromFile: false, description: "" },
                { id: "mylook", name: "My Look", needs: "screen",
                  fromFile: true, description: "" }
            ],
            audioSensitivityPct: function() { return 100 },
            rippleDecayPct: function() { return 100 },
            screenIndex: function() { return 0 }
        }
        b.renumber = function() {
            for (var i = 0; i < b.layers.length; i++)
                b.layers[i].index = i
        }
        b.effectLayerCount = function() { return b.layers.length }
        b.effectLayer = function(i) {
            return (i >= 0 && i < b.layers.length) ? b.layers[i] : {}
        }
        b.effectTargetIds = function() {
            return [ "desk", "keyboard_body", "ring", "mouse" ]
        }
        b.inputSourceState = function(src) {
            return b.inputStates[src]
                   || { name: src, enabled: false, ready: false,
                        status: "" }
        }
        b.beginEffectGesture    = function() { b.gestureDepth++ }
        b.commitEffectGesture   = function(l) {
            b.gestureDepth--; b.log.push("commit:" + l) }
        b.cancelEffectGesture   = function() {
            b.gestureDepth--; b.log.push("cancel") }
        b.moveEffectLayer = function(f, t) {
            b.log.push("move:" + f + ">" + t)
            if (f < 0 || t < 0 || f >= b.layers.length
                || t >= b.layers.length)
                return
            var l = b.layers.splice(f, 1)[0]
            b.layers.splice(t, 0, l)
            b.renumber()
        }
        b.addEffectLayer = function(prim) {
            b.log.push("add:" + prim)
            b.layers.push({ index: b.layers.length, primitive: prim,
                            enabled: true, blend: "replace",
                            opacity: 1, space: "world", speed: 0,
                            scale: 1, phase: 0, density: 1,
                            source: "", seed: 0,
                            origin: { x: 0, y: 0, z: 0 },
                            direction: { x: 0, y: 0, z: 0 },
                            path: [],
                            palette: [ { pos: 0, color: "#ffffff" } ],
                            targets: [] })
        }
        b.removeEffectLayer = function(i) {
            b.log.push("rm:" + i)
            if (i >= 0 && i < b.layers.length) {
                b.layers.splice(i, 1)
                b.renumber()
            }
        }
        b.setEffectLayerEnabled = function(i, on) {
            b.log.push("ena:" + i + "=" + on)
            b.layers[i].enabled = on
        }
        b.setEffectLayerBlend = function(i, bl) {
            b.log.push("blend:" + i + "=" + bl)
            b.layers[i].blend = bl
        }
        b.setEffectLayerOpacity = function(i, v) {
            b.layers[i].opacity = v
        }
        b.setEffectLayerField = function(i, f, v) {
            b.log.push("field:" + i + ":" + f + "=" + v)
            b.layers[i][f] = v
        }
        b.setEffectLayerSpace = function(i, local) {
            b.log.push("space:" + i + "=" + (local ? "local" : "world"))
            b.layers[i].space = local ? "local" : "world"
        }
        b.setEffectLayerOrigin = function(i, x, y, z) {
            b.layers[i].origin = { x: x, y: y, z: z }
        }
        b.setEffectLayerDirection = function(i, x, y, z) {
            b.layers[i].direction = { x: x, y: y, z: z }
        }
        b.setEffectLayerSource = function(i, s) {
            b.log.push("src:" + i + "=" + s)
            b.layers[i].source = s
        }
        b.setEffectLayerTargets = function(i, ts) {
            b.log.push("tgts:" + i + "=" + ts.join("+"))
            b.layers[i].targets = ts.slice()
        }
        b.addEffectLayerStop = function(i, pos, color) {
            b.log.push("stop+:" + i + "@" + pos.toFixed(2))
            b.layers[i].palette.push({ pos: pos, color: color })
            b.layers[i].palette.sort(function(a, s) {
                return a.pos - s.pos })
        }
        b.removeEffectLayerStop = function(i, s) {
            b.log.push("stop-:" + i + ":" + s)
            b.layers[i].palette.splice(s, 1)
        }
        b.moveEffectLayerStop = function(i, s, pos) {
            /* Mirror the real bridge: re-sort on every move and
               hand back the dragged stop's NEW index so a scrub
               tracks the stop across the sort. */
            var pal = b.layers[i].palette
            var st = pal[s]
            st.pos = pos
            pal.sort(function(a, c) { return a.pos - c.pos })
            return pal.indexOf(st)
        }
        b.setEffectLayerStopColor = function(i, s, c) {
            b.log.push("stopc:" + i + ":" + s + "=" + c)
            b.layers[i].palette[s].color = c
        }
        b.addEffectLayerPathPoint = function(i, x, y, z) {
            b.layers[i].path.push({ x: x, y: y, z: z })
        }
        b.setEffectLayerPathPoint = function(i, pt, x, y, z) {
            b.layers[i].path[pt] = { x: x, y: y, z: z }
        }
        b.removeEffectLayerPathPoint = function(i, pt) {
            b.log.push("pt-:" + i + ":" + pt)
            b.layers[i].path.splice(pt, 1)
        }
        b.resetEffectLayers = function() { b.resetCalls++ }
        b.saveLookAs = function(id, name) {
            b.saved = { id: id, name: name }
            if (id === "bad id")
                return { ok: false,
                         errors: [ "bad look id 'bad id'" ] }
            /* Mirror the real duplicate-id gate (review I3): an
               existing look id — file or shipped — is refused. */
            for (var k = 0; k < b.presetList.length; k++)
                if (b.presetList[k].id === id)
                    return { ok: false,
                             errors: [ "look id '" + id
                                       + "' already exists" ] }
            return { ok: true, id: id,
                     path: "presets/effects/" + id + ".effect.json" }
        }
        return b
    }

    function makePickCtl() {
        return { pickMode: "", pickPlaneY: 0, pickTarget: null }
    }

    function mk(comp, bridge, extra) {
        var c = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/"
            + comp))
        compare(c.status, Component.Ready, c.errorString())
        var o = c.createObject(tc)
        /* Panels size off their parent — the TestCase is 0-sized,
           which would keep effective visible=false (same workaround
           as tst_preset_editor). */
        if (o.width !== undefined && o.width <= 0)
            o.width = 800
        if (o.height !== undefined && o.height <= 0)
            o.height = 500
        o.bridgeOverride = bridge
        if (extra)
            for (var k in extra)
                o[k] = extra[k]
        if (o.fxStamp !== undefined)
            o.fxStamp++
        return o
    }

    /*---------------- tests ----------------*/

    function test_compileAll() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        var p = mk("PaletteEditor.qml", b, { layerIndex: 0 })
        var i = mk("EffectInspector.qml", b, { layerIndex: 0 })
        var e = mk("EffectEditor.qml", b)
        verify(s && p && i && e)
        s.destroy(); p.destroy(); i.destroy(); e.destroy()
    }

    function test_stackRows() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        var rows = s.rows()
        compare(rows.length, 3)
        compare(rows[0].primitive, "static")
        compare(rows[1].source, "key")
        compare(rows[2].enabled, false)
        s.destroy()
    }

    function test_stackModelShape() {
        /* {count,rowAt} object — the EffectLayerModel path. */
        var b = makeBridge()
        var rows = b.layers.map(rowOf)
        b.effectLayerModel = {
            rows: rows,
            count: function() { return this.rows.length },
            rowAt: function(i) { return this.rows[i] }
        }
        var s = mk("LayerStack.qml", b)
        compare(s.rows().length, 3)
        compare(s.rows()[2].primitive, "comet")
        s.destroy()
    }

    function test_stackOps() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        s.toggleEnabled({ index: 0, enabled: true })
        compare(b.log[0], "ena:0=false")
        compare(b.layers[0].enabled, false)

        s.moveRow(0, 1)
        compare(b.layers[0].primitive, "ripple",
                "move reorders the stub stack")
        compare(b.log[b.log.length - 1], "move:0>1")

        s.cycleBlend({ index: 0, blend: "replace" })
        compare(b.layers[0].blend, "add")
        s.cycleBlend({ index: 0, blend: "add" })
        compare(b.layers[0].blend, "screen")

        s.removeRow(2)
        compare(b.layers.length, 2)

        s.addLayer("wave")
        compare(b.layers.length, 3)
        compare(b.layers[2].primitive, "wave")
        compare(s.selIndex, 2, "new layer selects itself")
        s.destroy()
    }

    function test_opacityScrubIsOneGesture() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        s.opScrubBegin()
        for (var i = 0; i < 40; i++)
            s.opScrubMove(0, i / 40.0)
        s.opScrubEnd()
        compare(b.gestureDepth, 0)
        var commits = b.log.filter(function(l) {
            return l.indexOf("commit:") === 0 })
        compare(commits.length, 1, "100-move scrub = one commit")
        compare(Math.round(b.layers[0].opacity * 100) / 100, 0.98)
        s.destroy()
    }

    /* 5.2 review C1 regression: a mid-gesture effectLayersChanged
       (every preview move emits it through rebuildEffect) must NOT
       rebuild the delegate array — a rebuilt pressed slider dies
       mid-press, never sees onPressedChanged(false), and the
       gesture leaks open, silently swallowing every later edit.
       The row cache freezes for the drag; the deferred rebuild
       lands on commit. */
    function test_scrubSurvivesModelRefresh() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        compare(s.rowCache.length, 3)
        s.opScrubBegin()
        s.opScrubMove(0, 0.5)
        var before = s.rowCache
        s.poke()                 /* = effectLayersChanged mid-drag */
        verify(s.rowCache === before, "row cache frozen mid-gesture")
        verify(s.pendingRows, "rebuild deferred, not dropped")
        s.opScrubMove(0, 0.7)
        s.opScrubEnd()
        compare(b.gestureDepth, 0, "gesture closed exactly once")
        verify(s.rowCache !== before, "deferred rebuild applied")
        var commits = b.log.filter(function(l) {
            return l.indexOf("commit:") === 0 })
        compare(commits.length, 1, "interrupted scrub = one record")
        s.destroy()
    }

    /* Same class on the palette side — plus stop IDENTITY: the
       bridge re-sorts stops on every move, so a dragged stop
       crossing its neighbor changes index mid-gesture. */
    function test_paletteDragTracksStopAcrossSort() {
        var b = makeBridge()
        b.layers[0].palette = [ { pos: 0.1, color: "#aa0000" },
                                { pos: 0.5, color: "#00aa00" },
                                { pos: 0.9, color: "#0000aa" } ]
        var p = mk("PaletteEditor.qml", b, { layerIndex: 0 })
        compare(p.stopCache.length, 3)
        p.posScrubBegin(0)          /* grab the RED stop @0.1 */
        var cacheBefore = p.stopCache
        p.posScrubMove(0, 0.6)      /* crosses green -> re-sorted idx 1 */
        compare(p.dragStop, 1, "dragged stop followed the re-sort")
        p.poke()                    /* effectLayersChanged mid-drag */
        /* the model array froze for the whole drag (identity —
           the stub's stops alias their storage; the real bridge
           returns fresh maps, so only identity is meaningful). */
        verify(p.stopCache === cacheBefore,
               "stop rows frozen mid-gesture")
        verify(p.pendingStops, "rebuild deferred, not dropped")
        p.posScrubMove(0, 0.8)      /* stale row index still means RED */
        p.posScrubEnd()
        verify(!p.pendingStops, "deferred rebuild consumed on commit")
        compare(p.stopCache[1].pos, 0.8, "cache shows committed state")
        var pal = b.layers[0].palette   /* sorted: g .5, r .8, b .9 */
        compare(pal.length, 3)
        compare(pal[0].color, "#00aa00")
        compare(pal[0].pos, 0.5, "untouched neighbor kept its pos")
        compare(pal[1].color, "#aa0000")
        compare(pal[1].pos, 0.8, "the dragged stop kept moving")
        compare(b.gestureDepth, 0)
        p.destroy()
    }

    function test_srcBadgeStates() {
        var b = makeBridge()
        var s = mk("LayerStack.qml", b)
        compare(s.srcBadge("").text, "", "no source = no badge")
        var ok = s.srcBadge("audio")
        compare(ok.state, "ok")
        var off = s.srcBadge("key")
        compare(off.state, "off")
        compare(off.text, "needs key — off")
        var down = s.srcBadge("screen")
        compare(down.state, "down")
        verify(down.text.indexOf("capture unavailable") >= 0)
        s.destroy()
    }

    function test_paletteOps() {
        var b = makeBridge()
        var p = mk("PaletteEditor.qml", b, { layerIndex: 1 })
        compare(p.stops().length, 2)
        /* addPos = widest gap midpoint: [0,1] has a 0..1 interior
           gap of 1.0 -> 0.5. */
        compare(p.addPos(), 0.5)
        p.doAddStop()
        compare(b.layers[1].palette.length, 3)
        compare(b.layers[1].palette[1].pos, 0.5)

        p.setStopColor(0, "#00ff00")
        compare(b.layers[1].palette[0].color, "#00ff00")
        p.setStopColor(0, "not-a-color")
        compare(b.layers[1].palette[0].color, "#00ff00",
                "bad hex refused")

        p.posScrubBegin(1)
        p.posScrubMove(1, 0.4)
        p.posScrubMove(1, 0.6)
        p.posScrubEnd()
        compare(b.layers[1].palette[1].pos, 0.6)
        compare(b.gestureDepth, 0)
        var commits = b.log.filter(function(l) {
            return l.indexOf("commit:") === 0 })
        compare(commits.length, 1, "stop drag = one commit")

        p.removeStop(0)
        compare(b.layers[1].palette.length, 2)
        p.destroy()
    }

    function test_inspectorFields() {
        var b = makeBridge()
        var i = mk("EffectInspector.qml", b, { layerIndex: 1 })
        var l = i.layerMap()
        compare(l.primitive, "ripple")
        compare(l.source, "key")

        /* relevance table — ripple shows source+origin, hides
           direction/path/seed/phase */
        var f = i.fieldsFor("ripple")
        compare(f.source, 1); compare(f.origin, 1)
        verify(f.direction === undefined)
        verify(f.path === undefined)
        verify(f.seed === undefined)
        verify(f.phase === undefined)
        f = i.fieldsFor("comet")
        compare(f.path, 1); compare(f.speed, 1)
        verify(f.source === undefined)
        f = i.fieldsFor("static")
        compare(f.palette, 1)
        verify(f.speed === undefined)
        verify(f.origin === undefined)
        f = i.fieldsFor("screenfield")
        compare(f.needs, "screen")
        verify(f.palette === undefined)

        /* numeric + vec commits land on the stub stack */
        i.commitField("density", 2.25)
        compare(b.layers[1].density, 2.25)
        i.commitVec("origin", 0, 250)      /* mm -> m */
        compare(b.layers[1].origin.x, 0.25)
        compare(b.layers[1].origin.y, 0.02, "other axes kept")
        i.commitVec("direction", 2, -1)
        compare(b.layers[1].direction.z, -1)

        /* targets */
        i.addTarget("mouse")
        compare(b.layers[1].targets.join("+"), "desk+mouse")
        i.addTarget("mouse")
        compare(b.layers[1].targets.length, 2, "dup refused")
        i.addTarget("  ")
        compare(b.layers[1].targets.length, 2, "blank refused")
        i.removeTarget("desk")
        compare(b.layers[1].targets.join("+"), "mouse")

        /* path commit (switch to the comet layer) */
        i.layerIndex = 2
        i.commitPath(1, 0, 450)            /* mm -> m */
        compare(b.layers[2].path[1].x, 0.45)
        compare(b.layers[2].path[1].z, 0.3)
        i.destroy()
    }

    function test_inspectorNeedsBadge() {
        var b = makeBridge()
        var i = mk("EffectInspector.qml", b, { layerIndex: 0 })
        compare(i.needsBadge("").text, "")
        compare(i.needsBadge("key").state, "off")
        compare(i.needsBadge("screen").state, "down")
        compare(i.needsBadge("audio").state, "ok")
        i.destroy()
    }

    function test_editorSaveAs() {
        var b = makeBridge()
        var e = mk("EffectEditor.qml", b)
        e.open()
        /* Item.visible = effective visibility — always false under
           the zero-size TestCase parent; isOpen is the flag. */
        verify(e.isOpen)
        e.doSave("newlook", "My Look")
        compare(e.saveErrors.length, 0)
        compare(b.saved.id, "newlook")
        compare(e.savePath,
                "presets/effects/newlook.effect.json")
        e.doSave("bad id", "x")
        compare(e.saveErrors.length, 1)
        verify(e.saveErrors[0].indexOf("bad look id") >= 0)
        /* Duplicate-id gate (review I3): saving onto an existing
           look — shipped or file — refuses with a VISIBLE error. */
        e.doSave("aurora", "dup")
        compare(e.saveErrors.length, 1)
        verify(e.saveErrors[0].indexOf("already exists") >= 0)
        e.close()
        verify(!e.isOpen)
        e.destroy()
    }

    function test_editorPickMode() {
        var b = makeBridge()
        var ctl = makePickCtl()
        var e = mk("EffectEditor.qml", b, { pickCtl: ctl })
        e.open()
        e.selLayer = 1        /* ripple — has origin */

        /* arm origin pick — plane height follows the layer origin */
        e.armPick("origin")
        compare(ctl.pickMode, "origin")
        compare(ctl.pickPlaneY, 0.02)
        verify(ctl.pickTarget !== null)

        /* a click+drag delivers one gesture, one commit */
        e.onViewportPick(Qt.vector3d(0.2, 0.02, 0.1), "begin")
        e.onViewportPick(Qt.vector3d(0.3, 0.02, 0.15), "drag")
        e.onViewportPick(Qt.vector3d(0.35, 0.02, 0.18), "end")
        compare(b.layers[1].origin.x, 0.35)
        compare(b.layers[1].origin.y, 0.02, "y stays on the plane")
        compare(b.gestureDepth, 0)
        var commits = b.log.filter(function(l) {
            return l.indexOf("commit:") === 0 })
        compare(commits.length, 1)
        compare(commits[0], "commit:layer origin")

        /* path pick appends a waypoint and drags it */
        e.selLayer = 2        /* comet — has path */
        e.armPick("path")
        compare(ctl.pickMode, "path")
        e.onViewportPick(Qt.vector3d(0.1, 0, 0.1), "begin")
        compare(b.layers[2].path.length, 3, "click appends a point")
        e.onViewportPick(Qt.vector3d(0.2, 0, 0.2), "drag")
        compare(b.layers[2].path[2].x, 0.2, "drag moves the new point")
        e.onViewportPick(Qt.vector3d(0.2, 0, 0.2), "end")
        compare(b.gestureDepth, 0)

        /* cancel restores + disarms */
        e.onViewportPick(Qt.vector3d(0, 0, 0), "begin")
        e.onViewportPick(Qt.vector3d(0, 0, 0), "cancel")
        compare(ctl.pickMode, "")
        compare(b.gestureDepth, 0)
        verify(b.log.indexOf("cancel") >= 0)
        e.destroy()
    }

    function test_shelfNeedsBadge() {
        var b = makeBridge()
        var s = mk("LookShelf.qml", b)
        compare(s.needsBadge("").text, "")
        compare(s.needsBadge("audio").state, "ok")
        compare(s.needsBadge("key").state, "off")
        var d = s.needsBadge("screen")
        compare(d.state, "down")
        verify(d.text.indexOf("capture unavailable") >= 0)
        s.destroy()
    }
}
