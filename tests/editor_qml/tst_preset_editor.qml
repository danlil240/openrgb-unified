/*---------------------------------------------------------*\
||  tst_preset_editor.qml                                  ||
||                                                         ||
||  Task 4.3 device preset editor coverage — pure QML via ||
||  the signed qmltestrunner (same harness as             ||
||  tst_library.qml). DevicePresetEditor takes the        ||
||  bridgeOverride seam; the stub supplies presetDocument,||
||  previewPreset, savePresetType, convertZoneToPoints,   ||
||  hardwareControllers, instanceZoneState and the three  ||
||  binding ops — all as plain JS so no C++ context       ||
||  property is needed.                                   ||
||                                                         ||
||  Asserts:                                               ||
||    - the component compiles and instantiates           ||
||    - openForType loads the doc into the candidate      ||
||    - metadata/entity/zone edits land on the candidate  ||
||    - layout conversion + matrix map + points editing   ||
||    - saveErr validation + doSave routing + warnings    ||
||    - preview keeps the last good result on failure     ||
||    - instance binding section: pickers + ops route     ||
||    - openVariant forces save-as + fresh id             ||
||                                                         ||
||  NOT covered: the live SceneBridge write path and the  ||
||  View3D render — both need the C++ harness; the stub   ||
||  exercises the candidate/result plumbing only.         ||
\*---------------------------------------------------------*/
import QtQuick
import QtTest

TestCase {
    id: tc
    name: "PresetEditor"
    when: windowShown

    /*---------------- stubs ----------------*/
    function fanDoc() {
        return { id: "fan-120", name: "120 mm Fan", category: "fan",
            entities: {
                body: { geometry: "fan_body", size_m: [0.12, 0.025, 0.12],
                        zone: "ring",
                        appearance: { body_color: "#202020" } }
            },
            zones: [ { id: "ring", entity: "body", led_count: 8,
                       layout: { type: "ring", radius_m: 0.05,
                                 start_angle_deg: 30, face_y_m: 0.01,
                                 reverse: true } } ],
            binding_hints: [ { controller_name: "X870E",
                               vendor: "Gigabyte",
                               zone_name: "ARGB_1" } ] }
    }

    function makeBridge() {
        return {
            log: [],
            saved: null,
            zoneRows: [ { id: "ring", ledCount: 8, layout: "ring",
                          dynamic: false, binding: "b0", addrBase: 0,
                          verified: true,
                          bindingLabel: "X870E / ARGB_1" } ],
            presetModel: [ { typeId: "fan-120" }, { typeId: "desk" } ],
            presetDocument: function(tid) {
                this.log.push("doc:" + tid)
                if (tid === "")
                    return { exists: false }
                return { exists: true, doc: fanDoc(),
                         fromFile: true, packaged: false }
            },
            previewPreset: function(cand) {
                if (!cand || (cand.id || "") === "")
                    return { ok: false, errors: ["id required"] }
                return { ok: true,
                         objects: [ { nodeId: "body",
                                      geometry: "fan_body",
                                      pos: [0, 0, 0], rot: [0, 0, 0],
                                      size: [0.12, 0.025, 0.12],
                                      emitters: [] } ] }
            },
            savePresetType: function(cand, asNew) {
                this.saved = { cand: cand, asNew: asNew }
                if (cand.id === "bad")
                    return { ok: false, errors: ["schema: bad id"] }
                return { ok: true,
                         warnings: ["ring: bound 8 LEDs, type has 8"] }
            },
            convertZoneToPoints: function(cand, zi) {
                var c = JSON.parse(JSON.stringify(cand))
                var z = c.zones[zi]
                if (!z || z.layout.type === "points")
                    return { ok: false, errors: ["nothing to bake"] }
                z.layout = { type: "points",
                             points: [[0.05, 0.01, 0],
                                      [0, 0.01, 0.05]],
                             addresses: [0, 1] }
                z.led_count = 2
                /* The real bridge returns a QVariantMap — keys come
                   back alphabetical, so mimic that shuffle here to
                   prove the editor preserves entityOrder. */
                var sorted = {}
                Object.keys(c.entities).sort().forEach(
                    function(k) { sorted[k] = c.entities[k] })
                c.entities = sorted
                return { ok: true, candidate: c }
            },
            hardwareControllers: function() {
                return [ { index: 0, name: "X870E", vendor: "Gigabyte",
                           zones: [ { index: 0, name: "ARGB_1",
                                      leds: 8 } ] } ]
            },
            instanceZoneState: function(iid) {
                this.log.push("zstate:" + iid)
                return { ok: true, zones: this.zoneRows }
            },
            bindZoneToController: function(iid, zid, c, z, a, v) {
                this.log.push("bind:" + iid + "/" + zid + "/" + c
                              + "/" + z + "/" + a + "/" + v)
            },
            unbindZone: function(iid, zid) {
                this.log.push("unbind:" + iid + "/" + zid)
                this.zoneRows[0].binding = ""
            },
            setZoneParams: function(iid, zid, a, v) {
                this.log.push("params:" + iid + "/" + zid + "/"
                              + a + "/" + v)
            }
        }
    }

    function makeEd(bridge) {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/"
            + "DevicePresetEditor.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        var ed = comp.createObject(tc)
        /* The editor sizes itself off its parent; TestCase is
           0-sized, which would keep effective visible=false. */
        ed.width = 940
        ed.height = 640
        ed.bridgeOverride = bridge
        return ed
    }

    /*---------------- tests ----------------*/
    function test_compiles() {
        var ed = makeEd(null)
        verify(ed !== null)
        compare(ed.visible, false)
        ed.destroy()
    }

    function test_open_loads_candidate() {
        var b = makeBridge()
        var ed = makeEd(b)
        ed.openForType("fan-120", "")
        /* Item.visible reports *effective* visibility, which is
           always false under the zero-size TestCase parent — the
           loaded candidate is the open-state signal instead. */
        verify(ed.candidate !== null)
        compare(ed.candidate.id, "fan-120")
        compare(ed.candidate.name, "120 mm Fan")
        compare(ed.originalId, "fan-120")
        compare(ed.fromFile, true)
        compare(ed.saveAsMode, false)
        compare(ed.entityOrder, ["body"])
        compare(ed.candidate.entities.body.zone, "ring")
        compare(ed.candidate.zones.length, 1)
        compare(ed.candidate.zones[0].layout.start_angle_deg, 30)
        compare(ed.candidate.zones[0].layout.reverse, true)
        compare(ed.candidate.binding_hints.length, 1)
        /* type ids come from the preset model; the hw list from
           the adapter snapshot; no instance => no zone rows */
        compare(ed.typeIds.indexOf("desk") >= 0, true)
        compare(ed.hwList.length, 1)
        compare(ed.zoneRows.length, 0)
        ed.destroy()
    }

    function test_entity_and_zone_edits() {
        var ed = makeEd(makeBridge())
        ed.openForType("fan-120", "")
        /* entity transform + appearance */
        ed.setEntityPosMm("body", "x", 12)
        compare(ed.candidate.entities.body.x, 0.012)
        ed.setEntitySizeMm("body", 1, 30)
        compare(ed.candidate.entities.body.size_m[1], 0.03)
        ed.setEntityRotDeg("body", "ry", 45)
        compare(ed.candidate.entities.body.ry, 45)
        ed.setAppearanceJson("body", "{\"body_color\":\"#ff0000\",\"tag\":\"x\"}")
        compare(ed.candidate.entities.body.appearance.tag, "x")
        /* add + rename + remove entity */
        ed.addEntity()
        var nid = ed.entityOrder[ed.entityOrder.length - 1]
        verify(nid !== "body")
        ed.renameEntity(nid, "led-part")
        verify(ed.candidate.entities["led-part"] !== undefined)
        ed.removeEntity("led-part")
        verify(ed.candidate.entities["led-part"] === undefined)
        /* zone edits: field + layout switch + led_count sync */
        ed.setZoneField(0, "entity", "body")
        ed.setLayoutType(0, "points")
        compare(ed.candidate.zones[0].layout.type, "points")
        compare(ed.candidate.zones[0].led_count, 2)
        ed.addPoint(0)
        compare(ed.candidate.zones[0].layout.points.length, 3)
        compare(ed.candidate.zones[0].led_count, 3)
        ed.setPointMm(0, 2, 0, 7)
        compare(ed.candidate.zones[0].layout.points[2][0], 0.007)
        ed.setPointAddr(0, 2, 5)
        compare(ed.candidate.zones[0].layout.addresses[2], 5)
        ed.movePoint(0, 2, -1)
        compare(ed.candidate.zones[0].layout.points[1][0], 0.007)
        ed.removePoint(0, 0)
        compare(ed.candidate.zones[0].led_count, 2)
        /* matrix map text <-> cells */
        ed.setLayoutType(0, "matrix")
        compare(ed.matrixMapText(0), "0,1,2,3\n4,5,6,7")
        ed.setMatrixMapText(0, "0,-\n1,2\n3,4\n5,-")
        /* rows/cols still 2x4 => only first 2 rows used */
        ed.setLayoutParam(0, "rows", 4)
        ed.setLayoutParam(0, "cols", 2)
        ed.setMatrixMapText(0, "0,-\n1,2\n3,4\n5,-")
        var mp = ed.candidate.zones[0].layout.map
        compare(mp[1], 4294967295)   /* "-" => empty cell */
        compare(mp[7], 4294967295)
        compare(ed.candidate.zones[0].led_count, 6)
        ed.destroy()
    }

    function test_convert_zone_to_points() {
        var ed = makeEd(makeBridge())
        ed.openForType("fan-120", "")
        compare(ed.candidate.zones[0].layout.type, "ring")
        ed.convertZone(0)
        compare(ed.candidate.zones[0].layout.type, "points")
        compare(ed.candidate.zones[0].layout.addresses, [0, 1])
        compare(ed.candidate.zones[0].led_count, 2)
        /* re-convert refused: stub errors land in saveErrors */
        ed.convertZone(0)
        compare(ed.saveErrors.length > 0, true)
        ed.destroy()
    }

    function test_convert_preserves_entity_order() {
        /* doc deliberately authors entities out of alpha order —
           convertZoneToPoints returns them re-sorted (QVariantMap);
           the editor must keep the authored parts order. */
        var b = makeBridge()
        b.presetDocument = function(tid) {
            return { exists: true, fromFile: true, packaged: false,
                doc: { id: "multi", name: "Multi", category: "c",
                    entities: {
                        zz: { geometry: "box" },
                        aa: { geometry: "box" },
                        mm: { geometry: "box", zone: "ring" } },
                    zones: [ { id: "ring", entity: "mm", led_count: 4,
                               layout: { type: "ring", radius_m: 0.05,
                                         start_angle_deg: 0,
                                         face_y_m: 0.01,
                                         reverse: false } } ],
                    binding_hints: [] } }
        }
        var ed = makeEd(b)
        ed.openForType("multi", "")
        compare(ed.entityOrder, ["zz", "aa", "mm"])
        ed.convertZone(0)
        compare(ed.entityOrder, ["zz", "aa", "mm"])
        ed.destroy()
    }

    function test_layout_switch_and_accessors() {
        var ed = makeEd(makeBridge())
        ed.openForType("fan-120", "")
        /* selZone/selZoneLayout re-derive through stamp — a layout
           swap must be visible to every consumer immediately. */
        compare(ed.selZone().layout.type, "ring")
        ed.setLayoutType(0, "strip")
        compare(ed.selZone().layout.type, "strip")
        compare(ed.selZoneLayout().type, "strip")
        compare(ed.selZoneLayout().spacing_m, 0.005)
        ed.setLayoutType(0, "points")
        compare(ed.selZoneLayout().points.length, 2)
        ed.destroy()
    }

    function test_open_new_blank_candidate() {
        var b = makeBridge()
        var ed = makeEd(b)
        ed.openNew()
        verify(ed.candidate !== null)
        compare(ed.saveAsMode, true)
        compare(ed.candidate.id, "")
        compare(ed.originalId, "")
        compare(ed.instanceId, "")
        ed.destroy()
    }

    function test_save_click_surfaces_error() {
        /* Save stays clickable with an invalid candidate — the
           failure must land in the visible saveErrors list. */
        var b = makeBridge()
        var ed = makeEd(b)
        ed.openForType("fan-120", "")
        ed.candidate.name = ""
        ed.doSave()
        compare(ed.saveErrors, ["id: display name required"])
        verify(b.saved === null)
        ed.destroy()
    }

    function test_text_focus_flag() {
        var ed = makeEd(makeBridge())
        /* No window focus in the harness — the flag exists and is a
           bool; StudioScene reads it through focusPeer2. */
        compare(typeof ed.textFocus, "boolean")
        compare(ed.textFocus, false)
        ed.destroy()
    }

    function test_validation_and_save() {
        var b = makeBridge()
        var ed = makeEd(b)
        ed.openForType("fan-120", "")
        /* name required */
        ed.candidate.name = ""
        compare(ed.saveErr(), "display name required")
        ed.candidate.name = "Fan"
        compare(ed.saveErr(), "")
        /* bad id */
        ed.candidate.id = "bad id!"
        compare(ed.saveErr(), "id: letters, digits, _ and - only")
        /* save-as mode: existing id refused */
        ed.openVariant("fan-120", "")
        compare(ed.saveAsMode, true)
        compare(ed.candidate.id, "fan-120-copy")
        ed.candidate.id = "desk"      /* already in typeIds */
        compare(ed.saveErr(), "type id already exists")
        ed.candidate.id = "my-fan"
        /* save routes through the bridge; warnings surface */
        ed.doSave()
        verify(b.saved !== null)
        compare(b.saved.asNew, true)
        compare(b.saved.cand.id, "my-fan")
        compare(ed.saveWarnings.length, 1)
        compare(ed.originalId, "my-fan")
        compare(ed.saveAsMode, false)
        /* failed save -> saveErrors */
        ed.candidate.id = "bad"
        ed.saveAsMode = true
        ed.doSave()
        compare(ed.saveErrors, ["schema: bad id"])
        ed.destroy()
    }

    function test_preview_keeps_last_good() {
        var ed = makeEd(makeBridge())
        ed.openForType("fan-120", "")
        ed.refreshPreview()
        compare(ed.previewObjects.length, 1)
        compare(ed.previewError, "")
        /* invalidate -> error shows, objects retained */
        ed.candidate.id = ""
        ed.refreshPreview()
        compare(ed.previewError, "id required")
        compare(ed.previewObjects.length, 1)
        compare(ed.saveErrors, ["id required"])
        ed.destroy()
    }

    function test_instance_binding_section() {
        var b = makeBridge()
        var ed = makeEd(b)
        ed.openForType("fan-120", "fan0")
        compare(ed.instanceId, "fan0")
        compare(ed.zoneRows.length, 1)
        compare(ed.zoneRows[0].bindingLabel, "X870E / ARGB_1")
        /* unbind -> bridge op + refreshed rows */
        ed.unbindZone("ring")
        compare(b.log.indexOf("unbind:fan0/ring") >= 0, true)
        compare(ed.zoneRows[0].binding, "")
        /* bind with picker indices -> bindZoneToController */
        ed.bindZone("ring", 0, 0, 0, true)
        compare(b.log.indexOf("bind:fan0/ring/0/0/0/true") >= 0,
                true)
        ed.setZoneParams("ring", 4, false)
        compare(b.log.indexOf("params:fan0/ring/4/false") >= 0,
                true)
        ed.destroy()
    }
}
