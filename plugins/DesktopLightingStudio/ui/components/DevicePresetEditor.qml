/*---------------------------------------------------------*\
||  DevicePresetEditor.qml                                 ||
||                                                         ||
||  Device-preset editor pane (Studio Next task 4.3):      ||
||  modeless panel over the workspace editing one          ||
||  *.device.json candidate in memory. Nothing here        ||
||  touches the live scene, workspace doc, bindings or     ||
||  hardware — the candidate only leaves through           ||
||  bridge.savePresetType(), which writes the preset file  ||
||  via ConfigStore::WritePresetFile.                      ||
||                                                         ||
||  Open paths:                                            ||
||    openForType(typeId, instanceId)   library row Edit,  ||
||                                      or "Edit type" on  ||
||                                      a desk instance    ||
||    openVariant(typeId, instanceId)   "Edit as variant"  ||
||    openNew()                         fresh type         ||
||                                                         ||
||  Preview: the candidate is resolved on a THROWAWAY      ||
||  document + private registry copy (previewPreset) and   ||
||  rendered in a local View3D with the same family        ||
||  dispatch as StudioScene. Field edits are debounced     ||
||  ~150 ms; an invalid candidate keeps the last good      ||
||  preview and reports errors instead of blanking.        ||
||                                                         ||
||  Units: the file stores meters; every numeric field     ||
||  here shows millimeters (angles stay degrees).          ||
||                                                         ||
||  Binding section (instance mode) writes ONLY the        ||
||  per-instance device_settings.zones rows through the    ||
||  undoable bridge slots — a type edit never rebinds or   ||
||  resizes hardware. In library mode the same slot shows  ||
||  binding_hints — informational text only.               ||
\*---------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic
import QtQuick3D
import "../materials" as Mats

Rectangle {
    id: ped
    objectName: "presetEditor"
    visible: false
    color: th.panel
    radius: th.radiusLg
    border.color: th.borderHi
    width: parent ? Math.min(parent.width - 40, 940) : 940
    height: parent ? Math.min(parent.height - 40, 640) : 640
    z: 40

    /* ---------------- seams ---------------- */
    property var bridgeOverride: null
    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function hasBr() { return br() !== null }

    /* True while ANY control inside the panel holds focus — not
       just text fields: a focused button/combo must still swallow
       the viewport's W/E/P/F/Home/Ctrl+Z shortcuts. Walks the
       window's activeFocusItem ancestry so every control (incl.
       ones added later) is covered without per-field plumbing;
       StudioScene reads this through the focusPeer seam. */
    readonly property bool textFocus: {
        var w = ped.Window
        var f = w ? w.activeFocusItem : null
        while (f) {
            if (f === ped)
                return true
            f = f.parent
        }
        return false
    }
    function brQObj() {
        var b = br()
        return (b !== null && b.objectName !== undefined) ? b : null
    }

    /* ---------------- editor state ---------------- */
    /* The candidate is a plain JS object in the *.device.json
       shape (entities keyed by id, zones as an array). */
    property var candidate: null
    property var entityOrder: []        /* stable entity list order */
    property string originalId: ""      /* "" => never saved / new  */
    property string instanceId: ""      /* "" => library mode        */
    property bool fromFile: false
    property bool packaged: false
    property bool saveAsMode: false     /* Save-as: id editable      */
    property int stamp: 0               /* bump => refresh all reads */
    property string selEntityId: ""
    property int selZoneIdx: -1
    property var previewObjects: []     /* last GOOD preview result  */
    property string previewError: ""
    property var saveErrors: []
    property var saveWarnings: []
    property var typeIds: []            /* registry ids (child refs) */
    property var hwList: []             /* adapter snapshot          */
    property var zoneRows: []           /* instanceZoneState().zones */

    /* Geometry tags with family components — the combo lists them;
       any other tag stays valid as free text (generic primitive). */
    readonly property var knownGeometry:
        [ "desk", "case_shell", "monitor", "mouse_body", "mouse_zone",
          "gpu_body", "gpu_logo", "keyboard_body", "fan_body",
          "pump_body", "ram_body", "group", "box", "cylinder" ]
    readonly property var layoutTypes: [ "ring", "strip", "matrix", "points" ]

    Theme { id: th }

    Connections {
        target: ped.brQObj()
        function onPresetLibraryChanged() { ped.refreshTypeIds() }
        function onSceneChanged()         { ped.refreshZoneState() }
    }

    /* ================= candidate plumbing ================= */
    function blankCandidate() {
        return { schema_version: 1, id: "", name: "", category: "custom",
                 entities: {}, zones: [], binding_hints: [] }
    }

    function loadDoc(doc) {
        var c = blankCandidate()
        if (doc) {
            c.id       = doc.id       || ""
            c.name     = doc.name     || ""
            c.category = doc.category || ""
            var ents = doc.entities || {}
            c.entities = {}
            ped.entityOrder = []
            for (var k in ents) {
                var e = ents[k] || {}
                /* Normalize to the full field set — the editor
                   writes keys it manages; unknown appearance keys
                   ride along inside e.appearance untouched. */
                c.entities[k] = {
                    type:       e.type       || "",
                    geometry:   e.geometry   || "",
                    size_m:     e.size_m     || [0, 0, 0],
                    x: e.x || 0, y: e.y || 0, z: e.z || 0,
                    rx: e.rx || 0, ry: e.ry || 0, rz: e.rz || 0,
                    parent:     e.parent     || "",
                    zone:       e.zone       || "",
                    appearance: e.appearance || {}
                }
                ped.entityOrder.push(k)
            }
            var zs = doc.zones || []
            c.zones = []
            for (var i = 0; i < zs.length; i++) {
                var z = zs[i] || {}
                c.zones.push({
                    id: z.id || "",
                    entity: z.entity || "",
                    led_count: (z.led_count === undefined) ? 0 : z.led_count,
                    layout: z.layout || { type: "points", points: [] }
                })
            }
            c.binding_hints = doc.binding_hints || []
        } else {
            ped.entityOrder = []
        }
        candidate = c
        selEntityId = entityOrder.length ? entityOrder[0] : ""
        selZoneIdx = c.zones.length ? 0 : -1
        stamp++
        previewObjects = []
        previewError = ""
        previewTimer.restart()
    }

    function candidateJson() { return candidate }

    function touch() { stamp++; previewTimer.restart() }

    /* Registry ids for the child-type combo — same presetModel
       rowAt/count pipeline DeviceLibrary uses. */
    function refreshTypeIds() {
        var b = br()
        var out = []
        var m = (b && b.presetModel !== undefined) ? b.presetModel : null
        if (m !== null) {
            if (m.length !== undefined) {          /* array stub */
                for (var i = 0; i < m.length; i++)
                    out.push(m[i].typeId || "")
            } else if (typeof m.rowAt === "function") {
                var n = (typeof m.count === "function") ? m.count()
                                                      : m.count
                for (var r = 0; r < n; r++) {
                    var row = m.rowAt(r)
                    if (row && row.typeId !== undefined)
                        out.push(row.typeId)
                }
            }
        }
        typeIds = out
    }

    function refreshHw() {
        var b = br()
        hwList = (b && typeof b.hardwareControllers === "function")
               ? (b.hardwareControllers() || []) : []
    }

    function refreshZoneState() {
        var b = br()
        if (b && instanceId !== ""
            && typeof b.instanceZoneState === "function") {
            var r = b.instanceZoneState(instanceId)
            zoneRows = (r && r.zones) ? r.zones : []
        } else {
            zoneRows = []
        }
    }

    /* ================= open paths ================= */
    /* Library row "Edit" passes iid ""; "Edit type"/"Edit as
       variant" on a placed instance pass its id — the instance id
       only scopes the Binding section, never the file. */
    function openForType(tid, iid) {
        var b = br()
        instanceId  = iid || ""
        saveAsMode  = false
        saveErrors  = []
        saveWarnings = []
        var loaded = false
        if (b && typeof b.presetDocument === "function") {
            var r = b.presetDocument(tid)
            if (r && r.exists) {
                loadDoc(r.doc)
                originalId = tid
                fromFile   = r.fromFile === true
                packaged   = r.packaged === true
                loaded = true
            }
        }
        if (!loaded) {
            loadDoc(blankCandidate())
            candidate.id = tid || ""
            originalId = ""
            fromFile = false
            packaged = false
            stamp++
        }
        refreshTypeIds()
        refreshHw()
        refreshZoneState()
        visible = true
    }

    function openVariant(tid, iid) {
        openForType(tid, iid)
        saveAsMode = true
        candidate.id = (tid || "type") + "-copy"
        stamp++
    }

    function openNew() {
        openForType("", "")
        saveAsMode = true
    }

    function closeEditor() { visible = false }

    /* Escape closes the panel when it (not a field) holds focus —
       fields eat their own Escape first. */
    Keys.onEscapePressed: ped.closeEditor()
    onVisibleChanged: if (visible) forceActiveFocus()

    /* ================= entity model ================= */
    function entityIds() { return entityOrder.slice() }
    function ent(id) {
        stamp                       /* bindings calling ent() track edits */
        return (candidate && candidate.entities[id]) || null
    }
    function entityRows() {
        stamp
        var out = []
        for (var i = 0; i < entityOrder.length; i++) {
            var id = entityOrder[i]
            var e = ent(id)
            if (!e)
                continue
            out.push({ eid: id,
                       kind: (e.type || "") !== "" ? "ref " + e.type
                                                   : (e.geometry || "part"),
                       zone: e.zone || "" })
        }
        return out
    }
    function partIds() {           /* entities that may carry a zone */
        stamp
        var out = []
        for (var i = 0; i < entityOrder.length; i++) {
            var e = ent(entityOrder[i])
            if (e && (e.type || "") === "")
                out.push(entityOrder[i])
        }
        return out
    }

    function uniqueEntityId(base) {
        var n = 2, id = base
        while (candidate.entities[id] !== undefined)
            id = base + n++
        return id
    }

    function addEntity() {
        if (!candidate)
            return
        var id = uniqueEntityId("part")
        candidate.entities[id] = {
            type: "", geometry: "box", size_m: [0.1, 0.01, 0.1],
            x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0,
            parent: "", zone: "", appearance: {} }
        entityOrder.push(id)
        selEntityId = id
        touch()
    }

    function removeEntity(id) {
        if (!candidate || !candidate.entities[id])
            return
        delete candidate.entities[id]
        entityOrder = entityOrder.filter(function(x) { return x !== id })
        for (var k in candidate.entities)         /* dangling parent */
            if (candidate.entities[k].parent === id)
                candidate.entities[k].parent = ""
        for (var i = 0; i < candidate.zones.length; i++)
            if (candidate.zones[i].entity === id)
                candidate.zones[i].entity = ""
        if (selEntityId === id)
            selEntityId = entityOrder.length ? entityOrder[0] : ""
        touch()
    }

    function renameEntity(oldId, newId) {
        if (!candidate || newId === "" || newId === oldId
            || !candidate.entities[oldId]
            || candidate.entities[newId] !== undefined)
            return false
        candidate.entities[newId] = candidate.entities[oldId]
        delete candidate.entities[oldId]
        entityOrder = entityOrder.map(function(x) {
            return x === oldId ? newId : x })
        for (var k in candidate.entities) {
            if (candidate.entities[k].parent === oldId)
                candidate.entities[k].parent = newId
        }
        for (var i = 0; i < candidate.zones.length; i++) {
            if (candidate.zones[i].entity === oldId)
                candidate.zones[i].entity = newId
        }
        if (selEntityId === oldId)
            selEntityId = newId
        touch()
        return true
    }

    /* Generic field write. `type` is mutually exclusive with the
       part fields — switching to a child ref strips them so the
       saved file matches the schema contract. */
    function setEntityField(id, key, val) {
        var e = ent(id)
        if (!e)
            return
        e[key] = val
        if (key === "type" && val !== "") {
            delete e.geometry
            delete e.size_m
            delete e.zone
            delete e.appearance
        } else if (key === "type") {
            e.geometry = "box"
            e.size_m = [0.1, 0.01, 0.1]
            e.zone = ""
            e.appearance = {}
        }
        touch()
    }
    function setEntityPosMm(id, axis, mm) { var e = ent(id); if (e) { e[axis] = mm / 1000.0; touch() } }
    function setEntityRotDeg(id, axis, d) { var e = ent(id); if (e) { e[axis] = d; touch() } }
    function setEntitySizeMm(id, axis, mm) {
        var e = ent(id)
        if (!e)
            return
        if (!e.size_m || e.size_m.length !== 3)
            e.size_m = [0, 0, 0]
        e.size_m[axis] = Math.max(0, mm / 1000.0)
        touch()
    }
    function entSizeMm(e, axis) {
        return (e && e.size_m && e.size_m.length === 3)
             ? e.size_m[axis] * 1000.0 : 0
    }

    /* Appearance: body_color/roughness edit in place; the raw JSON
       field owns the whole object so unknown keys are preserved —
       never silently dropped. */
    function setAppearance(id, key, val) {
        var e = ent(id)
        if (!e || (e.type || "") !== "")
            return
        if (e.appearance === undefined || typeof e.appearance !== "object"
            || e.appearance === null)
            e.appearance = {}
        if (val === "" || val === undefined)
            delete e.appearance[key]
        else
            e.appearance[key] = val
        touch()
    }
    function appearanceJson(id) {
        stamp
        var e = ent(id)
        var a = (e && e.appearance) || {}
        return JSON.stringify(a)
    }
    function setAppearanceJson(id, text) {
        var e = ent(id)
        if (!e)
            return false
        var obj
        try { obj = JSON.parse(text) } catch (err) { return false }
        if (typeof obj !== "object" || obj === null)
            return false
        e.appearance = obj
        touch()
        return true
    }

    /* ================= zone model ================= */
    /* Selected-zone accessors — functions (not `property var`) so
       every consuming binding re-derives through `stamp`: a var
       bound to the same object identity suppresses notify, which
       left layout params and zone fields stale after setLayoutType
       swapped z.layout. */
    function selZone() {
        stamp
        return (candidate && selZoneIdx >= 0
                && selZoneIdx < candidate.zones.length)
             ? candidate.zones[selZoneIdx] : null
    }
    function selZoneLayout() {
        var z = selZone()
        return (z && z.layout) || {}
    }

    function zoneIds() {
        stamp
        var out = []
        if (candidate)
            for (var i = 0; i < candidate.zones.length; i++)
                out.push(candidate.zones[i].id || "")
        return out
    }
    function zoneRowsModel() {
        stamp
        var out = []
        if (!candidate)
            return out
        for (var i = 0; i < candidate.zones.length; i++)
            out.push({ idx: i, zid: candidate.zones[i].id || "",
                       summary: zoneSummary(i) })
        return out
    }
    /* Numeric readout beside the zone list — layout, LED count and
       the parameters that define order/orientation. */
    function zoneSummary(zi) {
        var z = candidate && candidate.zones[zi]
        if (!z)
            return ""
        var l = z.layout || {}
        var n = z.led_count || 0
        if (l.type === "ring")
            return "ring: " + n + " LEDs, start "
                 + Math.round(l.start_angle_deg || 0) + "°"
                 + (l.reverse ? ", reversed" : "")
        if (l.type === "strip")
            return "strip: " + n + " LEDs, "
                 + Math.round((l.spacing_m || 0) * 1000) + " mm pitch"
        if (l.type === "matrix") {
            if (l.dynamic)
                return "matrix: dynamic (hardware map)"
            var live = 0
            var mp = l.map || []
            var em = (l.empty === undefined) ? 4294967295 : l.empty
            for (var i = 0; i < mp.length; i++)
                if (mp[i] !== em)
                    live++
            return "matrix: " + (l.rows || 0) + "×" + (l.cols || 0)
                 + ", " + live + " LEDs"
        }
        return "points: " + ((l.points || []).length) + " emitters"
    }

    function addZone() {
        if (!candidate)
            return
        var base = "zone", n = 2, id = base
        var used = {}
        for (var i = 0; i < candidate.zones.length; i++)
            used[candidate.zones[i].id] = 1
        while (used[id])
            id = base + n++
        candidate.zones.push({ id: id, entity: "", led_count: 8,
            layout: { type: "ring", radius_m: 0.05,
                      start_angle_deg: 0, face_y_m: 0.01,
                      reverse: false } })
        selZoneIdx = candidate.zones.length - 1
        touch()
    }
    function removeZone(zi) {
        if (!candidate || zi < 0 || zi >= candidate.zones.length)
            return
        var zid = candidate.zones[zi].id
        candidate.zones.splice(zi, 1)
        for (var k in candidate.entities)          /* detach refs */
            if (candidate.entities[k].zone === zid)
                candidate.entities[k].zone = ""
        if (selZoneIdx >= candidate.zones.length)
            selZoneIdx = candidate.zones.length - 1
        touch()
    }
    function renameZone(zi, newId) {
        var z = candidate && candidate.zones[zi]
        if (!z || newId === "" || newId === z.id)
            return false
        for (var i = 0; i < candidate.zones.length; i++)
            if (i !== zi && candidate.zones[i].id === newId)
                return false
        var old = z.id
        z.id = newId
        for (var k in candidate.entities)
            if (candidate.entities[k].zone === old)
                candidate.entities[k].zone = newId
        touch()
        return true
    }
    function setZoneField(zi, key, val) {
        var z = candidate && candidate.zones[zi]
        if (!z)
            return
        if (key === "led_count")
            val = Math.max(0, Math.round(val))
        z[key] = val
        touch()
    }
    /* points/matrix layouts derive led_count from their own data —
       keep it in sync so the candidate validates as the user edits.
       ring/strip led_count stays user-authored. */
    function syncZoneLedCount(zi) {
        var z = candidate && candidate.zones[zi]
        if (!z || !z.layout)
            return
        var l = z.layout
        if (l.type === "points") {
            z.led_count = (l.points || []).length
        } else if (l.type === "matrix") {
            if (l.dynamic) {
                z.led_count = 0
            } else {
                var em = (l.empty === undefined) ? 4294967295 : l.empty
                var live = 0, mp = l.map || []
                for (var i = 0; i < mp.length; i++)
                    if (mp[i] !== em)
                        live++
                z.led_count = live
            }
        }
    }

    function setLayoutType(zi, t) {
        var z = candidate && candidate.zones[zi]
        if (!z || layoutTypes.indexOf(t) < 0)
            return
        if (t === "ring")
            z.layout = { type: "ring", radius_m: 0.05,
                         start_angle_deg: 0, face_y_m: 0.01,
                         reverse: false }
        else if (t === "strip")
            z.layout = { type: "strip", spacing_m: 0.005,
                         origin: [0, 0, 0] }
        else if (t === "matrix")
            z.layout = { type: "matrix", dynamic: false,
                         rows: 2, cols: 4, pitch_x_m: 0.02,
                         pitch_z_m: 0.02, origin: [0, 0, 0],
                         empty: 4294967295,
                         map: [0, 1, 2, 3, 4, 5, 6, 7] }
        else
            z.layout = { type: "points",
                         points: [[0.02, 0, 0], [-0.02, 0, 0]],
                         addresses: [] }
        syncZoneLedCount(zi)
        touch()
    }
    function setLayoutParam(zi, key, val) {
        var z = candidate && candidate.zones[zi]
        if (!z || !z.layout)
            return
        z.layout[key] = val
        /* rows/cols/empty re-shape the static map — re-pad it from
           its own text so the length always matches rows*cols. */
        if (z.layout.type === "matrix" && !z.layout.dynamic
            && (key === "rows" || key === "cols" || key === "empty")) {
            var txt = matrixMapText(zi)
            setMatrixMapText(zi, txt)
        }
        syncZoneLedCount(zi)
        touch()
    }
    function setLayoutOriginMm(zi, axis, mm) {
        var z = candidate && candidate.zones[zi]
        if (!z || !z.layout)
            return
        if (!z.layout.origin || z.layout.origin.length !== 3)
            z.layout.origin = [0, 0, 0]
        z.layout.origin[axis] = mm / 1000.0
        touch()
    }
    /* Matrix map <-> text: one line per row, comma-separated cell
       indices; "-" or "x" marks the empty cell. */
    function matrixMapText(zi) {
        stamp
        var z = candidate && candidate.zones[zi]
        var l = z && z.layout || {}
        var rows = l.rows || 0, cols = l.cols || 0
        var mp = l.map || []
        var em = (l.empty === undefined) ? 4294967295 : l.empty
        var lines = []
        for (var r = 0; r < rows; r++) {
            var cells = []
            for (var c = 0; c < cols; c++) {
                var v = mp[r * cols + c]
                cells.push(v === undefined || v === em ? "-" : String(v))
            }
            lines.push(cells.join(","))
        }
        return lines.join("\n")
    }
    function setMatrixMapText(zi, text) {
        var z = candidate && candidate.zones[zi]
        if (!z || !z.layout || z.layout.type !== "matrix")
            return false
        var rows = Math.round(z.layout.rows || 0)
        var cols = Math.round(z.layout.cols || 0)
        if (rows <= 0 || cols <= 0)
            return false
        var em = (z.layout.empty === undefined) ? 4294967295
                                                : z.layout.empty
        var mp = []
        var lines = text.split("\n")
        for (var r = 0; r < rows; r++) {
            var cells = (r < lines.length) ? lines[r].split(",") : []
            for (var c = 0; c < cols; c++) {
                var t = (c < cells.length) ? cells[c].trim() : "-"
                if (t === "-" || t === "x" || t === "" || isNaN(+t))
                    mp.push(em)
                else
                    mp.push(Math.max(0, Math.round(+t)))
            }
        }
        z.layout.map = mp
        syncZoneLedCount(zi)
        touch()
        return true
    }
    /* Points list: add/remove/reorder + per-point position (mm) and
       optional address. Reorder = LED order. */
    function pointRows(zi) {
        stamp
        var z = candidate && candidate.zones[zi]
        var l = z && z.layout || {}
        var pts = l.points || []
        var ad  = l.addresses || []
        var out = []
        for (var i = 0; i < pts.length; i++) {
            var p = pts[i] || [0, 0, 0]
            out.push({ idx: i, x: p[0] * 1000, y: p[1] * 1000,
                       z: p[2] * 1000,
                       addr: i < ad.length ? ad[i] : "" })
        }
        return out
    }
    function setPointMm(zi, i, axis, mm) {
        var l = candidate && candidate.zones[zi]
              && candidate.zones[zi].layout
        if (!l || !l.points || i < 0 || i >= l.points.length)
            return
        var p = l.points[i]
        if (!p || p.length !== 3)
            p = l.points[i] = [0, 0, 0]
        p[axis] = mm / 1000.0
        touch()
    }
    function setPointAddr(zi, i, a) {
        var l = candidate && candidate.zones[zi]
              && candidate.zones[zi].layout
        if (!l || !l.points || i < 0 || i >= l.points.length)
            return
        if (!l.addresses || l.addresses.length !== l.points.length) {
            l.addresses = []
            for (var k = 0; k < l.points.length; k++)
                l.addresses.push(k)
        }
        l.addresses[i] = Math.round(a)
        touch()
    }
    function addPoint(zi) {
        var l = candidate && candidate.zones[zi]
              && candidate.zones[zi].layout
        if (!l || l.type !== "points")
            return
        l.points = l.points || []
        l.points.push([0, 0, 0])
        if (l.addresses && l.addresses.length === l.points.length - 1)
            l.addresses.push(l.points.length - 1)
        syncZoneLedCount(zi)
        touch()
    }
    function removePoint(zi, i) {
        var l = candidate && candidate.zones[zi]
              && candidate.zones[zi].layout
        if (!l || !l.points || i < 0 || i >= l.points.length)
            return
        l.points.splice(i, 1)
        if (l.addresses) {
            if (i < l.addresses.length)
                l.addresses.splice(i, 1)
            if (l.addresses.length === 0)
                delete l.addresses
        }
        syncZoneLedCount(zi)
        touch()
    }
    function movePoint(zi, i, dir) {
        var l = candidate && candidate.zones[zi]
              && candidate.zones[zi].layout
        if (!l || !l.points)
            return
        var j = i + dir
        if (i < 0 || j < 0 || i >= l.points.length || j >= l.points.length)
            return
        var t = l.points[i]; l.points[i] = l.points[j]; l.points[j] = t
        if (l.addresses && l.addresses.length === l.points.length) {
            var a = l.addresses[i]
            l.addresses[i] = l.addresses[j]; l.addresses[j] = a
        }
        touch()
    }
    /* "Convert generated layout to points" — the bridge bakes
       positions + addresses so LED order is preserved verbatim;
       the layout link is broken by conversion. */
    function convertZone(zi) {
        var b = br()
        if (!b || typeof b.convertZoneToPoints !== "function")
            return
        var r = b.convertZoneToPoints(candidateJson(), zi)
        if (r && r.ok && r.candidate) {
            var keepId = candidate.zones[zi] ? candidate.zones[zi].id : ""
            var prevOrder = entityOrder.slice()
            candidate = r.candidate
            /* QVariantMap key order is alphabetical — keep the
               existing parts order, appending anything new and
               dropping removed ids, so the list doesn't reshuffle. */
            var ord = []
            for (var o = 0; o < prevOrder.length; o++)
                if (candidate.entities[prevOrder[o]] !== undefined)
                    ord.push(prevOrder[o])
            for (var k in candidate.entities)
                if (ord.indexOf(k) < 0)
                    ord.push(k)
            entityOrder = ord
            if (selEntityId !== "" && candidate.entities[selEntityId] === undefined)
                selEntityId = entityOrder.length ? entityOrder[0] : ""
            for (var i = 0; i < candidate.zones.length; i++)
                if (candidate.zones[i].id === keepId)
                    selZoneIdx = i
            touch()
        } else {
            saveErrors = (r && r.errors) ? r.errors
                                         : ["convert failed"]
        }
    }

    /* ================= binding hints (library mode) ================= */
    function addHint() {
        if (!candidate)
            return
        candidate.binding_hints = candidate.binding_hints || []
        candidate.binding_hints.push({ controller_name: "",
                                       vendor: "", zone_name: "" })
        touch()
    }
    function removeHint(i) {
        var h = candidate && candidate.binding_hints
        if (!h || i < 0 || i >= h.length)
            return
        h.splice(i, 1)
        touch()
    }
    function setHint(i, key, val) {
        var h = candidate && candidate.binding_hints
        if (!h || i < 0 || i >= h.length)
            return
        h[i][key] = val
        if (h[i][key] === "")
            delete h[i][key]
        touch()
    }

    /* ================= binding (instance mode) ================= */
    function bindZone(zid, ctrl, zone, addr, verified) {
        var b = br()
        if (b && typeof b.bindZoneToController === "function")
            b.bindZoneToController(instanceId, zid, ctrl, zone,
                                   Math.round(addr), verified)
        refreshZoneState()
    }
    function unbindZone(zid) {
        var b = br()
        if (b && typeof b.unbindZone === "function")
            b.unbindZone(instanceId, zid)
        refreshZoneState()
    }
    function setZoneParams(zid, addr, verified) {
        var b = br()
        if (b && typeof b.setZoneParams === "function")
            b.setZoneParams(instanceId, zid, Math.round(addr), verified)
        refreshZoneState()
    }

    /* ================= save ================= */
    function needsNew() { return saveAsMode || originalId === "" }
    function validTypeId(t) { return /^[A-Za-z0-9_-]+$/.test(t) }
    function saveErr() {
        stamp                       /* callers' bindings re-eval on edits */
        if (!candidate)
            return "no candidate"
        var tid = (candidate.id || "").trim()
        if (tid === "")
            return "type id required"
        if (!validTypeId(tid))
            return "id: letters, digits, _ and - only"
        if (needsNew() && typeIds.indexOf(tid) >= 0)
            return "type id already exists"
        if ((candidate.name || "").trim() === "")
            return "display name required"
        return ""
    }
    function doSave() {
        var b = br()
        if (!b || !candidate
            || typeof b.savePresetType !== "function")
            return
        var err = saveErr()
        if (err !== "") {
            saveErrors = ["id: " + err]
            return
        }
        var r = b.savePresetType(candidateJson(), needsNew())
        if (r && r.ok) {
            saveErrors = []
            saveWarnings = r.warnings || []
            originalId = candidate.id
            saveAsMode = false
            fromFile = true
            refreshTypeIds()
            refreshZoneState()
            previewTimer.restart()
        } else {
            saveErrors = (r && r.errors && r.errors.length)
                       ? r.errors : ["save failed"]
        }
    }

    /* ================= preview ================= */
    function refreshPreview() {
        var b = br()
        if (!b || !candidate
            || typeof b.previewPreset !== "function")
            return
        var r = b.previewPreset(candidateJson())
        if (r && r.ok) {
            previewObjects = r.objects || []
            previewError = ""
        } else {
            /* Invalid candidate — keep the last good preview. */
            previewError = ((r && r.errors) || ["invalid"]).join("; ")
        }
        saveErrors = (r && r.errors) ? r.errors : []
        framePreview()
    }
    Timer {
        id: previewTimer
        interval: 150
        repeat: false
        onTriggered: ped.refreshPreview()
    }

    /* ================= preview plumbing ================= */
    property var pvNodes: ({})
    function fixupPvParents() {
        for (var id in pvNodes) {
            var n = pvNodes[id]
            var p = (n.oParent && pvNodes[n.oParent]) || null
            /* A cyclic parent chain in the candidate must never
               produce a 3D parent cycle — walk p's ancestors and
               drop the link if n is in it. */
            var q = p
            while (q) {
                if (q === n) { p = null; break }
                q = q.parent
            }
            n.parent = p ? p : pvRoot
        }
    }
    /* Family dispatch — same table as StudioScene (duplicated per
       brief; the ~12 lines stay next to the preview so the live
       scene file is untouched). */
    function familySource(geom) {
        switch (geom) {
        case "keyboard_body": return "../devices/Keyboard.qml"
        case "mouse_body":    return "../devices/Mouse.qml"
        case "fan_body":
        case "pump_body":     return "../devices/Fan.qml"
        case "ram_body":      return "../devices/Ram.qml"
        case "case_shell":    return "../devices/Case.qml"
        case "gpu_body":      return "../devices/Gpu.qml"
        case "gpu_logo":      return "../devices/Strip.qml"
        case "monitor":       return "../devices/Monitor.qml"
        default:              return ""
        }
    }
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
    /* Orbit state for the preview camera — any drag orbits; wheel
       zooms. No middle-pan contract in this panel. */
    property real pvYaw: -30
    property real pvPitch: -25
    property real pvDist: 0.6
    property vector3d pvTarget: Qt.vector3d(0, 0.02, 0)
    function framePreview() {
        if (!previewObjects.length)
            return
        var sx = 0, sy = 0, sz = 0, r = 0.05
        for (var i = 0; i < previewObjects.length; i++) {
            var o = previewObjects[i]
            sx += o.x; sy += o.y; sz += o.z
        }
        var n = previewObjects.length
        var c = Qt.vector3d(sx / n, sy / n, sz / n)
        for (var j = 0; j < previewObjects.length; j++) {
            var oo = previewObjects[j]
            var d = Math.sqrt((oo.x - c.x) * (oo.x - c.x)
                            + (oo.y - c.y) * (oo.y - c.y)
                            + (oo.z - c.z) * (oo.z - c.z))
                  + 0.5 * Math.max(oo.bx, oo.by, oo.bz)
            if (d > r)
                r = d
        }
        pvTarget = c
        pvDist = Math.max(0.15, r * 2.2)
    }

    /* ================= small components ================= */
    /* Label + field row. */
    component FR: Row {
        property string label: ""
        property int lw: 58
        default property alias content: frInner.data
        spacing: th.spHalf
        height: 24
        Text {
            text: label
            width: lw
            anchors.verticalCenter: parent.verticalCenter
            color: th.textDim; font.pixelSize: th.fontSmall
            elide: Text.ElideRight
        }
        Row {
            id: frInner
            spacing: th.spHalf
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    /* Text field committing on Enter/focus-out. While focused the
       field owns its text; `value` refreshes unfocused fields after
       candidate edits (convert-to-points, renames). */
    component TF: TextField {
        property string value: ""
        property string own: ""
        property int fw: 150
        width: fw; height: 24
        text: activeFocus ? own : value
        onActiveFocusChanged: if (activeFocus) own = text
        onTextEdited: own = text
        signal committed(string t)
        onEditingFinished: committed(text)
        Keys.onShortcutOverride: function(e) {
            if (e.key === Qt.Key_Escape)
                e.accepted = true
        }
        Keys.onEscapePressed: focus = false
        selectByMouse: true
        color: th.text; font.pixelSize: th.fontBody
        placeholderTextColor: th.textFaint
        background: Rectangle {
            color: th.field; radius: th.radiusSm
            border.color: parent.activeFocus ? th.accent : th.borderHi
        }
    }

    /* Numeric field (mm or degrees — the caller converts). */
    component NF: TextField {
        property real value: 0
        property bool intOnly: false
        property string own: ""
        width: 62; height: 24
        function fmt(v) {
            return intOnly ? String(Math.round(v))
                           : String(Math.round(v * 100) / 100)
        }
        text: activeFocus ? own : fmt(value)
        onActiveFocusChanged: if (activeFocus) own = text
        onTextEdited: own = text
        signal committed(real v)
        onEditingFinished: {
            var v = parseFloat(text.replace(",", "."))
            if (isNaN(v)) { own = fmt(value); text = own }
            else committed(v)
        }
        Keys.onShortcutOverride: function(e) {
            if (e.key === Qt.Key_Escape)
                e.accepted = true
        }
        Keys.onEscapePressed: focus = false
        selectByMouse: true
        validator: RegularExpressionValidator {
            regularExpression: /-?[0-9]*[.,]?[0-9]*/
        }
        color: th.text; font.pixelSize: th.fontBody
        background: Rectangle {
            color: th.field; radius: th.radiusSm
            border.color: parent.activeFocus ? th.accent : th.borderHi
        }
    }

    /* ComboBox in the workspace palette; editable = free text.
       `watch` re-runs refresh() when the candidate mutates (renames
       fix parent/zone refs under the pick). */
    component Pick: ComboBox {
        id: pkRoot
        property var items: []
        property int watch: 0
        /* Guard: setTo() writes editText, which refires
           onEditTextChanged handlers that touch() the stamp —
           without the flag that's a binding loop. */
        property bool syncing: false
        model: items
        width: 150; height: 24
        editable: false
        onWatchChanged: refresh()
        function refresh() {}
        function setTo(v) {
            var i = items.indexOf(v)
            syncing = true
            if (i >= 0)
                currentIndex = i
            else if (editable)
                editText = v
            else
                currentIndex = -1
            syncing = false
        }
        font.pixelSize: th.fontBody
        contentItem: TextField {
            text: pkRoot.editable ? pkRoot.editText : pkRoot.displayText
            onTextEdited: if (pkRoot.editable) pkRoot.editText = text
            readOnly: !pkRoot.editable
            color: th.text; font.pixelSize: th.fontBody
            selectByMouse: true
            background: null
        }
        background: Rectangle {
            color: th.field; radius: th.radiusSm
            border.color: pkRoot.activeFocus ? th.accent : th.borderHi
        }
        delegate: ItemDelegate {
            width: ListView.view ? ListView.view.width : pkRoot.width
            contentItem: Text {
                text: modelData
                color: th.text; font.pixelSize: th.fontBody
            }
            background: Rectangle {
                color: highlighted ? th.selRow : th.panelAlt
            }
        }
        popup: Popup {
            y: pkRoot.height
            width: pkRoot.width
            implicitHeight: Math.min(contentItem.implicitHeight + 2, 160)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: pkRoot.delegateModel
                currentIndex: pkRoot.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle {
                color: th.panelAlt; border.color: th.borderHi
            }
        }
    }

    component Chk: Rectangle {
        property bool on: false
        property string label: ""
        signal toggled(bool v)
        width: chkRow.implicitWidth; height: 22
        color: "transparent"
        Row {
            id: chkRow
            spacing: th.spHalf
            Rectangle {
                width: 16; height: 16; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: parent.parent.on ? th.accentBg : th.field
                border.color: parent.parent.on ? th.accent : th.borderHi
                Text {
                    anchors.centerIn: parent
                    text: parent.parent.parent.on ? "✓" : ""
                    color: th.text; font.pixelSize: th.fontSmall
                }
            }
            Text {
                text: label
                anchors.verticalCenter: parent.verticalCenter
                color: th.textDim; font.pixelSize: th.fontSmall
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: parent.toggled(!parent.on)
        }
    }

    component Btn: Rectangle {
        property string text: ""
        property string tip: ""
        property bool primary: false
        signal clicked()
        height: 24; radius: th.radiusSm
        width: btxt.implicitWidth + th.sp2
        color: !enabled ? th.field
             : primary ? th.accentBg
             : (bma.containsMouse ? th.panelAlt : th.field)
        border.color: primary ? th.accent : th.borderHi
        opacity: enabled ? 1 : 0.45
        Text {
            id: btxt
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
        }
        MouseArea {
            id: bma; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            onClicked: parent.clicked()
        }
        ToolTip.visible: bma.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 400
    }

    /* ================= layout ================= */
    Column {
        anchors.fill: parent
        anchors.margins: th.sp2
        spacing: th.sp

        /* Title bar — drag moves the modeless panel. */
        Rectangle {
            width: parent.width; height: 30
            color: th.panelAlt; radius: th.radius
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 60   /* Close button keeps its area */
                property real dragX: 0
                property real dragY: 0
                onPressed: function(m) { dragX = m.x; dragY = m.y }
                onPositionChanged: function(m) {
                    if (!pressed || ped.parent === null)
                        return
                    ped.x = Math.max(0, Math.min(ped.parent.width - ped.width,
                                                 ped.x + m.x - dragX))
                    ped.y = Math.max(0, Math.min(ped.parent.height - ped.height,
                                                 ped.y + m.y - dragY))
                }
            }
            Row {
                x: th.sp; spacing: th.sp
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Device preset — " + (ped.candidate
                          ? (ped.candidate.id || "(new type)")
                          : "")
                          + (ped.instanceId !== ""
                             ? "  ·  instance " + ped.instanceId : "")
                    color: th.text; font.pixelSize: th.fontTitle
                    font.bold: true
                }
            }
            Btn {
                x: parent.width - width - th.spHalf
                anchors.verticalCenter: parent.verticalCenter
                text: "Close"
                onClicked: ped.closeEditor()
            }
        }

        Row {
            width: parent.width
            height: parent.height - 30 - th.sp
            spacing: th.sp2

            /* ---------- left: editable fields ---------- */
            Flickable {
                width: 430; height: parent.height
                contentWidth: formCol.width
                contentHeight: formCol.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: formCol
                    width: 430
                    spacing: th.sp

                    /* ===== Type ===== */
                    Text {
                        text: "Type"
                        color: th.text; font.pixelSize: th.fontBody
                        font.bold: true
                    }
                    FR {
                        label: "id"
                        TF {
                            id: idField
                            fw: 200
                            value: ped.candidate ? ped.candidate.id : ""
                            readOnly: !ped.needsNew()
                            enabled: ped.needsNew()
                            placeholderText: "type id"
                            onCommitted: function(t) {
                                if (ped.candidate)
                                    ped.candidate.id = t.trim()
                                ped.touch()
                            }
                        }
                    }
                    FR {
                        label: "name"
                        TF {
                            fw: 200
                            value: ped.candidate ? ped.candidate.name : ""
                            placeholderText: "display name"
                            onCommitted: function(t) {
                                if (ped.candidate)
                                    ped.candidate.name = t
                                ped.touch()
                            }
                        }
                    }
                    FR {
                        label: "category"
                        Pick {
                            id: catPick
                            editable: true
                            watch: ped.stamp
                            items: [ "fan", "keyboard", "mouse", "gpu",
                                     "case", "decor", "custom", "other" ]
                            function refresh() {
                                if (activeFocus)
                                    return
                                setTo(ped.candidate
                                      ? ped.candidate.category : "")
                            }
                            Component.onCompleted: refresh()
                            onActivated: function(i) {
                                if (ped.candidate && i >= 0)
                                    ped.candidate.category = items[i]
                                ped.touch()
                            }
                            onEditTextChanged: {
                                if (ped.candidate && ped.candidate.category !== editText) {
                                    ped.candidate.category = editText
                                    ped.touch()
                                }
                            }
                        }
                    }
                    Text {
                        visible: ped.packaged && !ped.fromFile
                        width: formCol.width
                        wrapMode: Text.WordWrap
                        text: "packaged type — saving creates a user "
                            + "override; delete the file to restore "
                            + "the packaged version"
                        color: th.textFaint; font.pixelSize: th.fontSmall
                    }

                    /* ===== Parts ===== */
                    Row {
                        spacing: th.spHalf
                        Text {
                            text: "Parts"
                            color: th.text; font.pixelSize: th.fontBody
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Btn { text: "+" ; tip: "Add part"
                              onClicked: ped.addEntity() }
                        Btn { text: "−"; tip: "Remove selected part"
                              enabled: ped.selEntityId !== ""
                              onClicked: ped.removeEntity(ped.selEntityId) }
                    }
                    Rectangle {
                        width: formCol.width; height: 66
                        color: th.field; radius: th.radiusSm
                        ListView {
                            anchors.fill: parent
                            anchors.margins: 2
                            clip: true
                            model: ped.entityRows()
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 20
                                radius: th.radiusSm
                                color: ped.selEntityId === modelData.eid
                                       ? th.selRow : "transparent"
                                Text {
                                    x: th.spHalf
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.eid + "  —  " + modelData.kind
                                        + (modelData.zone !== ""
                                           ? "  · zone " + modelData.zone : "")
                                    color: th.text; font.pixelSize: th.fontSmall
                                    elide: Text.ElideRight
                                    width: parent.width - th.sp
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ped.selEntityId = modelData.eid
                                }
                            }
                        }
                    }

                    /* Selected part editor. */
                    Column {
                        width: formCol.width
                        spacing: th.spHalf
                        visible: ped.ent(ped.selEntityId) !== null
                        /* Re-derive through ent() (which reads
                           stamp) — a `var` holding the same object
                           never re-notifies, so child-type picks
                           must not latch through a stale `e`. */
                        property bool isRef: {
                            var ent = ped.ent(ped.selEntityId)
                            return ent !== null
                                   && (ent.type || "") !== ""
                        }

                        FR {
                            label: "part id"
                            TF {
                                fw: 130
                                value: ped.selEntityId
                                onCommitted: function(t) {
                                    var id = t.trim()
                                    if (id !== "" && ped.validTypeId(id))
                                        ped.renameEntity(ped.selEntityId, id)
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "letters, digits, _ -"
                                color: th.textFaint; font.pixelSize: th.fontSmall
                            }
                        }
                        FR {
                            label: "child type"
                            Pick {
                                id: typePick
                                watch: ped.stamp
                                items: [ "" ].concat(ped.typeIds)
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    var e = ped.ent(ped.selEntityId)
                                    setTo(e ? (e.type || "") : "")
                                }
                                onActivated: function(i) {
                                    ped.setEntityField(ped.selEntityId,
                                        "type", i > 0 ? items[i] : "")
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "mounts another preset"
                                color: th.textFaint; font.pixelSize: th.fontSmall
                            }
                        }
                        FR {
                            label: "geometry"
                            visible: !parent.isRef
                            Pick {
                                editable: true
                                watch: ped.stamp
                                items: ped.knownGeometry
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    var e = ped.ent(ped.selEntityId)
                                    setTo(e ? (e.geometry || "") : "")
                                }
                                onActivated: function(i) {
                                    if (i >= 0)
                                        ped.setEntityField(ped.selEntityId,
                                                           "geometry", items[i])
                                }
                                onEditTextChanged: {
                                    if (!syncing && editText !== "")
                                        ped.setEntityField(ped.selEntityId,
                                                           "geometry", editText)
                                }
                            }
                        }
                        FR {
                            label: "size mm"
                            visible: !parent.isRef
                            NF { value: ped.entSizeMm(ped.ent(ped.selEntityId), 0)
                                 onCommitted: function(v) { ped.setEntitySizeMm(ped.selEntityId, 0, v) } }
                            NF { value: ped.entSizeMm(ped.ent(ped.selEntityId), 1)
                                 onCommitted: function(v) { ped.setEntitySizeMm(ped.selEntityId, 1, v) } }
                            NF { value: ped.entSizeMm(ped.ent(ped.selEntityId), 2)
                                 onCommitted: function(v) { ped.setEntitySizeMm(ped.selEntityId, 2, v) } }
                        }
                        FR {
                            label: "pos mm"
                            NF { value: ((ped.ent(ped.selEntityId) || {}).x || 0) * 1000
                                 onCommitted: function(v) { ped.setEntityPosMm(ped.selEntityId, "x", v) } }
                            NF { value: ((ped.ent(ped.selEntityId) || {}).y || 0) * 1000
                                 onCommitted: function(v) { ped.setEntityPosMm(ped.selEntityId, "y", v) } }
                            NF { value: ((ped.ent(ped.selEntityId) || {}).z || 0) * 1000
                                 onCommitted: function(v) { ped.setEntityPosMm(ped.selEntityId, "z", v) } }
                        }
                        FR {
                            label: "rot °"
                            NF { value: (ped.ent(ped.selEntityId) || {}).rx || 0
                                 onCommitted: function(v) { ped.setEntityRotDeg(ped.selEntityId, "rx", v) } }
                            NF { value: (ped.ent(ped.selEntityId) || {}).ry || 0
                                 onCommitted: function(v) { ped.setEntityRotDeg(ped.selEntityId, "ry", v) } }
                            NF { value: (ped.ent(ped.selEntityId) || {}).rz || 0
                                 onCommitted: function(v) { ped.setEntityRotDeg(ped.selEntityId, "rz", v) } }
                        }
                        FR {
                            label: "parent"
                            Pick {
                                watch: ped.stamp
                                items: [ "" ].concat(ped.entityIds().filter(
                                    function(x) { return x !== ped.selEntityId }))
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    var e = ped.ent(ped.selEntityId)
                                    setTo(e ? (e.parent || "") : "")
                                }
                                onActivated: function(i) {
                                    ped.setEntityField(ped.selEntityId,
                                        "parent", i > 0 ? items[i] : "")
                                }
                            }
                        }
                        FR {
                            label: "zone"
                            visible: !parent.isRef
                            Pick {
                                watch: ped.stamp
                                items: [ "" ].concat(ped.zoneIds())
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    var e = ped.ent(ped.selEntityId)
                                    setTo(e ? (e.zone || "") : "")
                                }
                                onActivated: function(i) {
                                    ped.setEntityField(ped.selEntityId,
                                        "zone", i > 0 ? items[i] : "")
                                }
                            }
                        }

                        /* Appearance — body_color/roughness edit in
                           place; the raw JSON line owns the whole
                           object so unknown keys survive a save. */
                        FR {
                            label: "color"
                            visible: !parent.isRef
                            Rectangle {
                                width: 22; height: 16; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                border.color: th.borderHi
                                color: {
                                    ped.stamp
                                    var e = ped.ent(ped.selEntityId)
                                    var a = e && e.appearance || {}
                                    var c = a.body_color || ""
                                    return /^#[0-9a-fA-F]{6}$/.test(c)
                                         ? c : th.field
                                }
                            }
                            TF {
                                fw: 80
                                placeholderText: "#RRGGBB"
                                value: {
                                    ped.stamp
                                    var e = ped.ent(ped.selEntityId)
                                    var a = e && e.appearance || {}
                                    return a.body_color || ""
                                }
                                onCommitted: function(t) {
                                    var v = t.trim()
                                    if (v === ""
                                        || /^#[0-9a-fA-F]{6}$/.test(v))
                                        ped.setAppearance(ped.selEntityId,
                                                          "body_color", v)
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "rough"
                                color: th.textDim; font.pixelSize: th.fontSmall
                            }
                            NF {
                                width: 52
                                value: {
                                    ped.stamp
                                    var e = ped.ent(ped.selEntityId)
                                    var a = e && e.appearance || {}
                                    var r = a.roughness
                                    return (r === undefined) ? 0 : r
                                }
                                onCommitted: function(v) {
                                    v = Math.max(0, Math.min(1, v))
                                    if (v === 0)
                                        ped.setAppearance(ped.selEntityId,
                                                          "roughness", "")
                                    else
                                        ped.setAppearance(ped.selEntityId,
                                                          "roughness", v)
                                }
                            }
                        }
                        FR {
                            label: "appear."
                            visible: !parent.isRef
                            TF {
                                fw: 280
                                value: ped.appearanceJson(ped.selEntityId)
                                placeholderText: "{\"body_color\":\"#…\"}"
                                onCommitted: function(t) {
                                    if (!ped.setAppearanceJson(
                                            ped.selEntityId, t))
                                        ped.saveErrors =
                                            ["appearance: not a JSON object"]
                                }
                            }
                        }
                    }

                    /* ===== Zones ===== */
                    Row {
                        spacing: th.spHalf
                        Text {
                            text: "Zones"
                            color: th.text; font.pixelSize: th.fontBody
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Btn { text: "+"; tip: "Add zone"
                              onClicked: ped.addZone() }
                        Btn { text: "−"; tip: "Remove selected zone"
                              enabled: ped.selZoneIdx >= 0
                              onClicked: ped.removeZone(ped.selZoneIdx) }
                    }
                    Rectangle {
                        width: formCol.width; height: 60
                        color: th.field; radius: th.radiusSm
                        ListView {
                            anchors.fill: parent
                            anchors.margins: 2
                            clip: true
                            model: ped.zoneRowsModel()
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width
                                height: 20
                                radius: th.radiusSm
                                color: ped.selZoneIdx === modelData.idx
                                       ? th.selRow : "transparent"
                                Text {
                                    x: th.spHalf
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - th.sp
                                    text: modelData.zid + "  —  "
                                        + modelData.summary
                                    color: th.text; font.pixelSize: th.fontSmall
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: ped.selZoneIdx = modelData.idx
                                }
                            }
                        }
                    }

                    /* Selected zone editor. */
                    Column {
                        id: zoneEditor
                        width: formCol.width
                        spacing: th.spHalf
                        visible: ped.selZoneIdx >= 0
                                 && ped.candidate
                                 && ped.selZoneIdx < ped.candidate.zones.length

                        FR {
                            label: "zone id"
                            TF {
                                fw: 110
                                value: ped.selZone() ? ped.selZone().id : ""
                                onCommitted: function(t) {
                                    var id = t.trim()
                                    if (id !== "" && ped.validTypeId(id))
                                        ped.renameZone(ped.selZoneIdx, id)
                                }
                            }
                        }
                        FR {
                            label: "entity"
                            Pick {
                                watch: ped.stamp
                                items: [ "" ].concat(ped.partIds())
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    setTo(ped.selZone()
                                          ? (ped.selZone().entity || "") : "")
                                }
                                onActivated: function(i) {
                                    ped.setZoneField(ped.selZoneIdx,
                                        "entity", i > 0 ? items[i] : "")
                                }
                            }
                        }
                        FR {
                            label: "led_count"
                            NF {
                                intOnly: true
                                value: ped.selZone()
                                       ? ped.selZone().led_count : 0
                                onCommitted: function(v) {
                                    ped.setZoneField(ped.selZoneIdx,
                                                     "led_count", v)
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "0 = dynamic"
                                color: th.textFaint; font.pixelSize: th.fontSmall
                            }
                        }
                        FR {
                            label: "layout"
                            Pick {
                                watch: ped.stamp
                                items: ped.layoutTypes
                                Component.onCompleted: refresh()
                                function refresh() {
                                    if (activeFocus)
                                        return
                                    setTo(ped.selZoneLayout().type || "points")
                                }
                                onActivated: function(i) {
                                    if (i >= 0
                                        && items[i] !== ped.selZoneLayout().type)
                                        ped.setLayoutType(ped.selZoneIdx,
                                                          items[i])
                                }
                            }
                        }

                        /* --- ring params --- */
                        Column {
                            width: parent.width
                            spacing: th.spHalf
                            visible: ped.selZoneLayout().type === "ring"
                            FR {
                                label: "radius mm"
                                NF { value: (ped.selZoneLayout().radius_m || 0) * 1000
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "radius_m", v / 1000.0) } }
                            }
                            FR {
                                label: "start °"
                                NF { value: ped.selZoneLayout().start_angle_deg || 0
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "start_angle_deg", v) } }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "face Y"
                                    color: th.textDim; font.pixelSize: th.fontSmall
                                }
                                NF { value: (ped.selZoneLayout().face_y_m || 0) * 1000
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "face_y_m", v / 1000.0) } }
                            }
                            Chk {
                                label: "reverse direction"
                                on: ped.selZoneLayout().reverse === true
                                onToggled: function(v) {
                                    ped.setLayoutParam(ped.selZoneIdx,
                                                       "reverse", v)
                                }
                            }
                        }

                        /* --- strip params --- */
                        Column {
                            width: parent.width
                            spacing: th.spHalf
                            visible: ped.selZoneLayout().type === "strip"
                            FR {
                                label: "spacing mm"
                                NF { value: (ped.selZoneLayout().spacing_m || 0) * 1000
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "spacing_m", v / 1000.0) } }
                            }
                            FR {
                                label: "origin mm"
                                NF { value: ((ped.selZoneLayout().origin || [0,0,0])[0]) * 1000
                                     onCommitted: function(v) { ped.setLayoutOriginMm(ped.selZoneIdx, 0, v) } }
                                NF { value: ((ped.selZoneLayout().origin || [0,0,0])[1]) * 1000
                                     onCommitted: function(v) { ped.setLayoutOriginMm(ped.selZoneIdx, 1, v) } }
                                NF { value: ((ped.selZoneLayout().origin || [0,0,0])[2]) * 1000
                                     onCommitted: function(v) { ped.setLayoutOriginMm(ped.selZoneIdx, 2, v) } }
                            }
                        }

                        /* --- matrix params --- */
                        Column {
                            width: parent.width
                            spacing: th.spHalf
                            visible: ped.selZoneLayout().type === "matrix"
                            Chk {
                                label: "dynamic — emitter map comes "
                                     + "from the bound hardware zone"
                                on: ped.selZoneLayout().dynamic === true
                                onToggled: function(v) {
                                    ped.setLayoutParam(ped.selZoneIdx,
                                                       "dynamic", v)
                                }
                            }
                            FR {
                                label: "rows × cols"
                                visible: !ped.selZoneLayout().dynamic
                                NF { intOnly: true
                                     value: ped.selZoneLayout().rows || 0
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "rows", Math.max(0, Math.round(v))) } }
                                NF { intOnly: true
                                     value: ped.selZoneLayout().cols || 0
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "cols", Math.max(0, Math.round(v))) } }
                            }
                            FR {
                                label: "pitch mm"
                                NF { value: (ped.selZoneLayout().pitch_x_m || 0) * 1000
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "pitch_x_m", v / 1000.0) } }
                                NF { value: (ped.selZoneLayout().pitch_z_m || 0) * 1000
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "pitch_z_m", v / 1000.0) } }
                            }
                            FR {
                                label: "empty"
                                visible: !ped.selZoneLayout().dynamic
                                NF { intOnly: true
                                     width: 110
                                     value: (ped.selZoneLayout().empty === undefined)
                                          ? 4294967295 : ped.selZoneLayout().empty
                                     onCommitted: function(v) {
                                         ped.setLayoutParam(ped.selZoneIdx,
                                             "empty", Math.max(0, Math.round(v))) } }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "empty-cell marker"
                                    color: th.textFaint; font.pixelSize: th.fontSmall
                                }
                            }
                            Column {
                                width: parent.width
                                spacing: 2
                                visible: !ped.selZoneLayout().dynamic
                                Text {
                                    text: "map — one row per line, "
                                        + "comma cells, '-' = empty"
                                    color: th.textFaint; font.pixelSize: th.fontSmall
                                }
                                TextArea {
                                    width: parent.width
                                    height: 60
                                    /* Same own-text pattern as TF:
                                       while focused the field shows
                                       `own`, so an unrelated commit's
                                       stamp bump can't clobber
                                       in-progress typing; unfocused it
                                       re-syncs from the candidate
                                       (zone switch, convert, undo). */
                                    property string own: ""
                                    text: activeFocus ? own
                                        : ped.matrixMapText(
                                              ped.selZoneIdx)
                                    onActiveFocusChanged:
                                        if (activeFocus) own = text
                                    onTextChanged:
                                        if (activeFocus) own = text
                                    font.family: "monospace"
                                    font.pixelSize: th.fontSmall
                                    color: th.text
                                    onEditingFinished: {
                                        if (!ped.setMatrixMapText(
                                                ped.selZoneIdx, text))
                                            ped.saveErrors =
                                                ["matrix map: bad input"]
                                    }
                                    background: Rectangle {
                                        color: th.field; radius: th.radiusSm
                                        border.color: parent.activeFocus
                                                      ? th.accent : th.borderHi
                                    }
                                }
                            }
                        }

                        /* --- points params --- */
                        Column {
                            width: parent.width
                            spacing: th.spHalf
                            visible: ped.selZoneLayout().type === "points"
                            Row {
                                spacing: th.spHalf
                                Text {
                                    text: "points — explicit emitter "
                                        + "offsets (mm); order = LED order"
                                    color: th.textFaint
                                    font.pixelSize: th.fontSmall
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Btn { text: "+"; tip: "Add point"
                                      onClicked: ped.addPoint(ped.selZoneIdx) }
                            }
                            Repeater {
                                model: ped.pointRows(ped.selZoneIdx)
                                delegate: Row {
                                    required property var modelData
                                    spacing: th.spHalf
                                    height: 24
                                    Text {
                                        text: modelData.idx
                                        width: 18
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: th.textFaint
                                        font.pixelSize: th.fontSmall
                                    }
                                    NF { value: modelData.x
                                         onCommitted: function(v) {
                                             ped.setPointMm(ped.selZoneIdx,
                                                 modelData.idx, 0, v) } }
                                    NF { value: modelData.y
                                         onCommitted: function(v) {
                                             ped.setPointMm(ped.selZoneIdx,
                                                 modelData.idx, 1, v) } }
                                    NF { value: modelData.z
                                         onCommitted: function(v) {
                                             ped.setPointMm(ped.selZoneIdx,
                                                 modelData.idx, 2, v) } }
                                    NF { intOnly: true; width: 44
                                         value: modelData.addr === ""
                                                ? modelData.idx : modelData.addr
                                         onCommitted: function(v) {
                                             ped.setPointAddr(ped.selZoneIdx,
                                                 modelData.idx, v) } }
                                    Btn { text: "↑"; height: 20
                                          onClicked: ped.movePoint(ped.selZoneIdx,
                                              modelData.idx, -1) }
                                    Btn { text: "↓"; height: 20
                                          onClicked: ped.movePoint(ped.selZoneIdx,
                                              modelData.idx, 1) }
                                    Btn { text: "×"; height: 20
                                          onClicked: ped.removePoint(ped.selZoneIdx,
                                              modelData.idx) }
                                }
                            }
                        }

                        Btn {
                            visible: ped.selZoneLayout().type !== "points"
                                     && !(ped.selZoneLayout().type === "matrix"
                                          && ped.selZoneLayout().dynamic)
                            text: "Convert generated layout to points"
                            tip: "Bakes generated emitter positions + "
                               + "addresses into an explicit points "
                               + "list — the layout link is broken "
                               + "by conversion"
                            onClicked: ped.convertZone(ped.selZoneIdx)
                        }
                    }

                    /* ===== Binding hints / binding ===== */
                    Column {
                        width: formCol.width
                        spacing: th.spHalf
                        visible: ped.instanceId === ""
                        Text {
                            text: "Binding hints"
                            color: th.text; font.pixelSize: th.fontBody
                            font.bold: true
                        }
                        Text {
                            text: "compatible hardware hints — does "
                                + "not bind or verify"
                            width: formCol.width
                            wrapMode: Text.WordWrap
                            color: th.textFaint; font.pixelSize: th.fontSmall
                        }
                        Repeater {
                            model: {
                                ped.stamp
                                return (ped.candidate
                                        && ped.candidate.binding_hints)
                                     ? ped.candidate.binding_hints : []
                            }
                            delegate: Row {
                                required property var modelData
                                required property int index
                                spacing: th.spHalf
                                height: 24
                                TF {
                                    fw: 110
                                    placeholderText: "controller_name"
                                    value: modelData.controller_name || ""
                                    onCommitted: function(t) {
                                        ped.setHint(index,
                                            "controller_name", t.trim()) }
                                }
                                TF {
                                    fw: 70
                                    placeholderText: "vendor"
                                    value: modelData.vendor || ""
                                    onCommitted: function(t) {
                                        ped.setHint(index, "vendor",
                                                    t.trim()) }
                                }
                                TF {
                                    fw: 80
                                    placeholderText: "zone_name"
                                    value: modelData.zone_name || ""
                                    onCommitted: function(t) {
                                        ped.setHint(index, "zone_name",
                                                    t.trim()) }
                                }
                                Btn {
                                    text: "×"; height: 20
                                    onClicked: ped.removeHint(index)
                                }
                            }
                        }
                        Btn {
                            text: "+ hint"
                            onClicked: ped.addHint()
                        }
                    }

                    /* Instance mode — per-instance hardware binding.
                       Same pickers the diagnostics drawer uses; the
                       write lands in device_settings.zones.<id> via
                       the undoable bridge ops. */
                    Column {
                        width: formCol.width
                        spacing: th.spHalf
                        visible: ped.instanceId !== ""
                        Text {
                            text: "Binding — " + ped.instanceId
                            color: th.text; font.pixelSize: th.fontBody
                            font.bold: true
                        }
                        Text {
                            width: formCol.width
                            wrapMode: Text.WordWrap
                            text: "bindings are per-instance — editing "
                                + "the type does not rebind hardware"
                            color: th.textFaint; font.pixelSize: th.fontSmall
                        }
                        Repeater {
                            model: ped.zoneRows
                            delegate: Column {
                                required property var modelData
                                width: formCol.width
                                spacing: 2
                                property var zr: modelData
                                property var picks: ({})
                                Text {
                                    text: zr.id + " — " + zr.layout
                                        + (zr.dynamic ? " (dynamic)"
                                           : ", " + zr.ledCount + " LEDs")
                                    color: th.text; font.pixelSize: th.fontSmall
                                }
                                /* Bound: label + params + unbind. */
                                Row {
                                    spacing: th.spHalf
                                    visible: (zr.binding || "") !== ""
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 160
                                        elide: Text.ElideRight
                                        text: zr.bindingLabel || zr.binding
                                        color: th.ok; font.pixelSize: th.fontSmall
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "addr"
                                        color: th.textDim; font.pixelSize: th.fontSmall
                                    }
                                    NF {
                                        intOnly: true; width: 44
                                        value: zr.addrBase || 0
                                        onCommitted: function(v) {
                                            ped.setZoneParams(zr.id, v,
                                                              zr.verified) }
                                    }
                                    Chk {
                                        label: "verified"
                                        on: zr.verified === true
                                        onToggled: function(v) {
                                            ped.setZoneParams(zr.id,
                                                zr.addrBase || 0, v) }
                                    }
                                    Btn {
                                        text: "Unbind"; height: 20
                                        onClicked: ped.unbindZone(zr.id)
                                    }
                                }
                                /* Unbound: controller/zone pickers. */
                                Row {
                                    spacing: th.spHalf
                                    visible: (zr.binding || "") === ""
                                    Pick {
                                        id: ctrlPick
                                        width: 140
                                        items: {
                                            var out = []
                                            for (var i = 0;
                                                 i < ped.hwList.length; i++)
                                                out.push("[" + i + "] "
                                                    + ped.hwList[i].name)
                                            return out
                                        }
                                        property int ctrl: -1
                                        onActivated: function(i) { ctrl = i }
                                    }
                                    Pick {
                                        id: zPick
                                        width: 110
                                        property int zone: -1
                                        items: {
                                            var c = ctrlPick.ctrl
                                            var out = []
                                            if (c >= 0 && c < ped.hwList.length) {
                                                var zs = ped.hwList[c].zones || []
                                                for (var i = 0; i < zs.length; i++)
                                                    out.push(zs[i].name + " ("
                                                        + zs[i].leds + ")")
                                            }
                                            return out
                                        }
                                        onActivated: function(i) { zone = i }
                                    }
                                    NF {
                                        id: addrF
                                        intOnly: true; width: 44
                                        value: 0
                                        onCommitted: function(v) {
                                            addrF.value =
                                                Math.max(0, Math.round(v))
                                        }
                                    }
                                    Btn {
                                        text: "Bind"; height: 20
                                        enabled: ctrlPick.ctrl >= 0
                                                 && zPick.zone >= 0
                                        onClicked: ped.bindZone(zr.id,
                                            ctrlPick.ctrl, zPick.zone,
                                            addrF.value, true)
                                    }
                                }
                            }
                        }
                        Text {
                            visible: ped.zoneRows.length === 0
                            text: "no zones on this type"
                            color: th.textFaint; font.pixelSize: th.fontSmall
                        }
                    }

                    /* ===== messages + save ===== */
                    Column {
                        width: formCol.width
                        spacing: 2
                        visible: ped.saveErrors.length > 0
                        Repeater {
                            model: ped.saveErrors
                            delegate: Text {
                                required property var modelData
                                width: formCol.width
                                wrapMode: Text.WordWrap
                                text: modelData
                                color: th.bad; font.pixelSize: th.fontSmall
                            }
                        }
                    }
                    Column {
                        width: formCol.width
                        spacing: 2
                        visible: ped.saveWarnings.length > 0
                        Repeater {
                            model: ped.saveWarnings
                            delegate: Text {
                                required property var modelData
                                width: formCol.width
                                wrapMode: Text.WordWrap
                                text: modelData
                                color: th.warn; font.pixelSize: th.fontSmall
                            }
                        }
                    }
                    Row {
                        spacing: th.spHalf
                        Btn {
                            primary: true
                            text: ped.needsNew() ? "Save new type"
                                                 : "Save type"
                            /* Stays enabled — a failing save routes
                               saveErr() into the visible saveErrors
                               list (doSave); gating silently is how
                               dead buttons happen. */
                            enabled: ped.candidate !== null
                            tip: "Write presets/devices/<id>.device.json "
                               + "— never touches the workspace"
                            onClicked: ped.doSave()
                        }
                        Btn {
                            visible: ped.originalId !== ""
                            text: ped.saveAsMode ? "editing new id…"
                                                 : "Save as new…"
                            onClicked: {
                                ped.saveAsMode = true
                                ped.candidate.id = ped.originalId + "-copy"
                                ped.stamp++
                            }
                        }
                    }
                }
            }

            /* ---------- right: preview + readout ---------- */
            Column {
                width: parent.width - 430 - th.sp2
                height: parent.height
                spacing: th.spHalf

                Rectangle {
                    width: parent.width
                    height: parent.height - readout.height - errLine.height
                          - th.sp * 2
                    color: th.field; radius: th.radius
                    border.color: th.border
                    clip: true

                    View3D {
                        id: pview
                        anchors.fill: parent
                        /* Cheap tier — the preview is a small pane,
                           not the desk viewport. */
                        environment: Mats.StudioEnvironment {
                            quality: "low"
                            bloomPref: false
                        }
                        /* Orbit rig — target/yaw/pitch/distance. */
                        Node {
                            id: pvOrigin
                            position: ped.pvTarget
                            eulerRotation.x: ped.pvPitch
                            eulerRotation.y: ped.pvYaw
                            PerspectiveCamera {
                                clipNear: 0.005
                                clipFar: 50
                                fieldOfView: 45
                                position: Qt.vector3d(0, 0, ped.pvDist)
                            }
                        }
                        DirectionalLight { eulerRotation.x: -50; brightness: 1.4 }
                        DirectionalLight {
                            eulerRotation.y: 140; brightness: 0.5
                        }
                        Node {
                            id: pvRoot
                            Repeater3D {
                                id: pvRep
                                model: ped.previewObjects
                                onModelChanged: ped.pvNodes = ({})
                                delegate: Node {
                                    id: pobj
                                    function rf(name, dflt) {
                                        var v = modelData[name]
                                        return v === undefined ? dflt : v
                                    }
                                    property string oId: rf("id", "")
                                    property string oGeom: rf("geometry", "")
                                    property string oParent: rf("parentId", "")
                                    property real oBx: rf("bx", 0)
                                    property real oBy: rf("by", 0)
                                    property real oBz: rf("bz", 0)
                                    property string oKind: rf("kind", "decor")
                                    property var spec: ped.bodySpec(oGeom)
                                    readonly property bool ghostBody:
                                        spec !== null && spec.ghost === true
                                    property var emitterPos: rf("emitters", [])
                                    property var emitterColors: []

                                    position: Qt.vector3d(rf("x", 0),
                                                          rf("y", 0),
                                                          rf("z", 0))
                                    rotation: Qt.quaternion(rf("qw", 1),
                                        rf("qx", 0), rf("qy", 0),
                                        rf("qz", 0))
                                    scale: Qt.vector3d(rf("sx", 1),
                                        rf("sy", 1), rf("sz", 1))
                                    visible: rf("visible", true)

                                    Component.onCompleted: {
                                        ped.pvNodes[oId] = pobj
                                        var p = ped.pvNodes[oParent]
                                        if (p === pobj)
                                            p = null
                                        pobj.parent = p ? p : pvRoot
                                        var cols = []
                                        var n = emitterPos.length
                                        for (var i = 0; i < n; i++) {
                                            /* order ramp: LED 0 = ok,
                                               lightening accent ramp */
                                            cols.push(i === 0 ? th.ok
                                                : Qt.lighter(th.accent,
                                                    1 + 0.9 * i
                                                      / Math.max(1, n - 1)))
                                        }
                                        emitterColors = cols
                                        if (Object.keys(ped.pvNodes).length
                                            === pvRep.count)
                                            ped.fixupPvParents()
                                    }
                                    Component.onDestruction: {
                                        if (ped.pvNodes[oId] === pobj)
                                            delete ped.pvNodes[oId]
                                    }

                                    Loader3D {
                                        source: ped.familySource(pobj.oGeom)
                                        onLoaded: item.ctx = pobj
                                    }
                                    Model {
                                        visible: pobj.spec !== null
                                            && ped.familySource(pobj.oGeom)
                                               === ""
                                        source: pobj.spec ? pobj.spec.src
                                                          : "#Cube"
                                        scale: pobj.spec
                                            ? Qt.vector3d(pobj.oBx / 100,
                                                          pobj.oBy / 100,
                                                          pobj.oBz / 100)
                                            : Qt.vector3d(0, 0, 0)
                                        materials: PrincipledMaterial {
                                            baseColor: pobj.spec
                                                ? pobj.spec.c : "#000000"
                                            opacity: (pobj.spec
                                                      && pobj.spec.ghost)
                                                     ? 0.35 : 1.0
                                        }
                                    }
                                    /* Emitter dots — static display
                                       colors; index 0 is marked so
                                       the generated order reads. */
                                    Repeater3D {
                                        model: pobj.emitterPos
                                        delegate: Model {
                                            required property var modelData
                                            source: "#Sphere"
                                            position: Qt.vector3d(
                                                modelData.x, modelData.y,
                                                modelData.z)
                                            scale: Qt.vector3d(0.011,
                                                               0.011,
                                                               0.011)
                                            materials: PrincipledMaterial {
                                                lighting: PrincipledMaterial
                                                          .NoLighting
                                                baseColor:
                                                    (modelData.i
                                                     < pobj.emitterColors.length)
                                                    ? pobj.emitterColors[modelData.i]
                                                    : th.accent
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    /* Any drag orbits; wheel zooms. */
                    MouseArea {
                        anchors.fill: parent
                        property real lx: 0
                        property real ly: 0
                        onPressed: function(m) { lx = m.x; ly = m.y }
                        onPositionChanged: function(m) {
                            if (!pressed)
                                return
                            ped.pvYaw += (m.x - lx) * 0.4
                            ped.pvPitch = Math.max(-89,
                                Math.min(89, ped.pvPitch - (m.y - ly) * 0.4))
                            lx = m.x; ly = m.y
                        }
                        onWheel: function(w) {
                            ped.pvDist = Math.max(0.05,
                                ped.pvDist * (w.angleDelta.y > 0
                                              ? 0.9 : 1.1))
                        }
                    }
                    Text {
                        x: th.spHalf; y: th.spHalf
                        text: "preview"
                        color: th.textFaint; font.pixelSize: th.fontSmall
                    }
                }

                /* Preview status — invalid candidates keep the last
                   good render and report here. */
                Text {
                    id: errLine
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: ped.previewError !== "" ? th.bad : th.textFaint
                    font.pixelSize: th.fontSmall
                    text: ped.previewError !== ""
                          ? "candidate invalid — showing last good: "
                            + ped.previewError
                          : ped.previewObjects.length + " object(s)"
                }

                /* Zone readout — order/orientation numbers beside
                   the list. */
                Column {
                    id: readout
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: ped.zoneRowsModel()
                        delegate: Text {
                            required property var modelData
                            text: modelData.zid + ": " + modelData.summary
                            color: th.textDim; font.pixelSize: th.fontSmall
                        }
                    }
                }
            }
        }
    }
}
