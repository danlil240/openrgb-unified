/*---------------------------------------------------------*\
||  DeviceLibrary.qml                                      ||
||                                                         ||
||  Device-type catalog for the workspace shell (Studio    ||
||  Next task 4.2). Rows come from the bridge's           ||
||  presetModel (PresetListModel over PresetRegistry::    ||
||  List()); a plain-JS stub — array of row maps or a     ||
||  {count,rowAt} object — drives the same pipeline in    ||
||  the qmltestrunner suite.                              ||
||                                                         ||
||  - search + category chips filter the merged listing   ||
||  - favorites pin first, then alphabetical              ||
||  - rows: glyph thumbnail (category initial — no asset  ||
||    pipeline), name, dims in mm, LED total, source      ||
||    badge (file|packaged), star toggle, add button      ||
||  - rows drag onto the viewport: the chip carries       ||
||    Drag.keys ["studio-device-type"] + dropTypeId;      ||
||    StudioScene's DropArea maps the pixel to the desk   ||
||    plane and calls bridge.addDeviceInstance            ||
||  - footer: "Save variant…" (exactly one instance       ||
||    selected) and "Create preset from selection…"       ||
||    (1..n) open one shared id/name dialog                ||
||                                                         ||
||  Bridge write surface (all validated + statused C++    ||
||  side; failures never touch scene/workspace/files):    ||
||    addDeviceInstance(typeId, x, y, z)                  ||
||    setTypeFavorite(typeId, fav)                        ||
||    saveInstanceAsVariant(inst, newTypeId, name)        ||
||    createTypeFromSelection(instanceIds, newTypeId, name)            ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: lib
    objectName: "deviceLibrary"
    color: th.panel

    /* Same seam as DeviceTree/StudioWorkspace: the plugin supplies
       the `bridge` context property; qmltestrunner injects a stub
       via bridgeOverride. sceneView is the StudioScene instance —
       used for the button-add's viewport-center placement (drag/drop
       lands on the scene's own DropArea). */
    property var bridgeOverride: null
    property var sceneView: null
    /* Task 4.3 — the row "Edit" action opens the preset editor for
       the type; the header "New" button opens a blank one. (The
       workspace hosts the panel in both cases.) */
    signal editRequested(string typeId)
    signal newRequested()
    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function hasBr() { return br() !== null }
    /* Connections targets must be real QObjects — qmltestrunner
       stubs are plain JS values (no objectName property). */
    function brQObj() {
        var b = br()
        return (b !== null && b.objectName !== undefined) ? b : null
    }

    /* Re-eval stamps — presetLibraryChanged/selectionChanged bump
       them on a real bridge; the stub path pokes them by hand. */
    property int stamp: 0
    property int selStamp: 0

    property string search: ""
    property string category: ""      /* "" = All */

    Theme { id: th }

    Connections {
        target: lib.brQObj()
        function onPresetLibraryChanged() { lib.stamp++ }
        function onSelectionChanged()     { lib.selStamp++ }
    }

    /*===================== data pipeline ====================*/
    /* Merged registry rows as plain maps — QAbstractListModel
       ({count(),rowAt(i)}) or a bare array (stub). */
    function libRows() {
        lib.stamp                      /* re-read on refresh */
        var b = br()
        var m = (b && b.presetModel !== undefined) ? b.presetModel : null
        var rows = []
        if (m === null)
            return rows
        if (m.length !== undefined) {            /* array stub */
            for (var i = 0; i < m.length; i++)
                rows.push(m[i])
            return rows
        }
        if (typeof m.rowAt === "function") {
            var n = (typeof m.count === "function") ? m.count()
                                                  : m.count
            for (var r = 0; r < n; r++) {
                var row = m.rowAt(r)
                if (row && row.typeId !== undefined)
                    rows.push(row)
            }
        }
        return rows
    }

    function allTypeIds() {
        var rows = libRows()
        var out = []
        for (var i = 0; i < rows.length; i++)
            out.push(rows[i].typeId)
        return out
    }

    /* Derived category list (sorted, unique) for the chip row. */
    function categories() {
        var rows = libRows()
        var seen = {}, out = []
        for (var i = 0; i < rows.length; i++) {
            var c = rows[i].category || "misc"
            if (!seen[c]) {
                seen[c] = 1
                out.push(c)
            }
        }
        out.sort()
        return out
    }

    /* Search + category filter, then favorites-first alphabetical.
       Pure JS — tst_library.qml exercises this directly. */
    function filteredRows() {
        var q = lib.search.toLowerCase()
        var rows = libRows()
        var out = []
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i]
            var cat = r.category || "misc"
            if (lib.category !== "" && cat !== lib.category)
                continue
            if (q !== ""
                && (r.name   || "").toLowerCase().indexOf(q) < 0
                && (r.typeId || "").toLowerCase().indexOf(q) < 0
                && cat.toLowerCase().indexOf(q) < 0)
                continue
            out.push(r)
        }
        out.sort(function(a, b) {
            var fa = a.favorite ? 1 : 0
            var fb = b.favorite ? 1 : 0
            if (fa !== fb)
                return fb - fa                    /* favorites first */
            var na = (a.name || a.typeId || "").toLowerCase()
            var nb = (b.name || b.typeId || "").toLowerCase()
            if (na !== nb)
                return na < nb ? -1 : 1           /* then A-Z */
            return (a.typeId || "") < (b.typeId || "") ? -1 : 1
        })
        return out
    }

    /* One compute shared by the list and the empty-state checks. */
    property var shown: lib.filteredRows()
    property int totalCount: lib.libRows().length

    function selIds() {
        lib.selStamp
        var b = br()
        return (b && b.selectedInstances) ? b.selectedInstances : []
    }

    function toggleFav(tid, on) {
        var b = br()
        if (b && typeof b.setTypeFavorite === "function")
            b.setTypeFavorite(tid, on)
        lib.stamp++                  /* stub path has no NOTIFY */
    }

    /* Button add: viewport-center desk-plane point via the scene's
       camera (same planeHitPoint math the move gesture uses);
       falls back to the camera target, then the origin. */
    function addAtCenter(tid) {
        var b = br()
        if (!b || typeof b.addDeviceInstance !== "function")
            return
        var p = null
        var sv = lib.sceneView
        if (sv && sv.editorCam) {
            p = sv.editorCam.planeHitPoint(sv.width / 2,
                                           sv.height / 2, 1, 0.0)
            if (!p && sv.editorCam.target)
                p = sv.editorCam.target
        }
        b.addDeviceInstance(tid, p ? p.x : 0, 0, p ? p.z : 0)
    }

    /* Dialog validation — mirrors the bridge's IsPresetId charset
       so obvious rejects never leave QML (the C++ still re-checks). */
    function validTypeId(t) { return /^[A-Za-z0-9_-]+$/.test(t) }
    function dlgError(tid, nm) {
        if (tid === "")
            return "type id required"
        if (!validTypeId(tid))
            return "id: letters, digits, _ and - only"
        if (lib.allTypeIds().indexOf(tid) >= 0)
            return "type id already exists"
        if (nm === "")
            return "display name required"
        return ""
    }

    /* Dialog dispatch — one seam for the ok button AND the QML
       test (the dialog internals aren't reachable by id from a
       test object). mode "variant" repoints `inst`; "assembly"
       builds a child-ref type from the CURRENT selection — the
       bridge validates the explicit id list itself. */
    function submitNewType(mode, inst, tid, nm) {
        var b = br()
        if (!b)
            return
        if (mode === "variant") {
            if (typeof b.saveInstanceAsVariant === "function")
                b.saveInstanceAsVariant(inst, tid, nm)
        } else if (typeof b.createTypeFromSelection === "function") {
            b.createTypeFromSelection(selIds(), tid, nm)
        }
    }

    /* Compact text button (toolbar set — matches DeviceTree's). */
    component LBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property string tip: ""
        property alias  ma: lma
        signal clicked()
        height: 22; radius: th.radiusSm
        color: active ? th.accentBg
                      : (lma.containsMouse ? th.panelAlt : th.field)
        border.color: lma.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
        }
        MouseArea {
            id: lma; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: (lma.containsMouse || lma.activeFocus)
                         && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    /* Small square icon button for row actions (star / add). */
    component IconBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property string tip: ""
        signal clicked()
        width: 26; height: 22; radius: th.radiusSm
        color: active ? th.accentBg
                      : (ihov.containsMouse ? th.panelAlt : th.field)
        border.color: ihov.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.active ? th.accent
                   : (parent.enabled ? th.text : th.textFaint)
            font.pixelSize: th.fontSmall
        }
        MouseArea {
            id: ihov; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: ihov.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    /*======================= layout =========================*/
    Column {
        anchors.fill: parent
        anchors.margins: th.sp
        spacing: th.sp

        Row {
            width: parent.width; spacing: th.spHalf
            Text {
                text: "Library"
                color: th.text; font.pixelSize: th.fontTitle
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: lib.totalCount + " types"
                color: th.textFaint; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: 1; height: 1 }
            IconBtn {
                text: "✚"
                tip: "New type — open the preset editor on a blank "
                   + "candidate"
                onClicked: lib.newRequested()
            }
        }

        /* Search — same field chrome + Escape-eats-itself as the
           device tree (the scene's Escape handler stays out). */
        TextField {
            id: searchField
            width: parent.width
            height: 28
            placeholderText: "Search types…"
            placeholderTextColor: th.textFaint
            color: th.text
            font.pixelSize: th.fontBody
            onTextChanged: lib.search = text
            Keys.onShortcutOverride: function(e) {
                if (e.key === Qt.Key_Escape)
                    e.accepted = true
            }
            Keys.onEscapePressed: {
                if (text !== "")
                    text = ""
                else
                    focus = false
            }
            background: Rectangle {
                color: th.field; radius: th.radiusSm
                border.color: searchField.activeFocus ? th.accent
                                                      : th.borderHi
            }
        }

        /* Category chips — derived from the merged listing. */
        Flow {
            width: parent.width
            spacing: th.spHalf
            Repeater {
                model: {
                    var cats = lib.categories()
                    var out = [{ label: "All", cat: "" }]
                    for (var i = 0; i < cats.length; i++)
                        out.push({ label: cats[i], cat: cats[i] })
                    return out
                }
                delegate: Rectangle {
                    required property var modelData
                    height: 18
                    width: chipText.width + 12
                    radius: th.radiusSm
                    color: lib.category === modelData.cat ? th.accentBg
                           : (chipMa.containsMouse ? th.panelAlt
                                                   : th.field)
                    border.color: lib.category === modelData.cat
                                  ? th.accent : th.borderHi
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: lib.category === modelData.cat ? th.text
                                                              : th.textDim
                        font.pixelSize: th.fontSmall
                    }
                    MouseArea {
                        id: chipMa
                        anchors.fill: parent; hoverEnabled: true
                        onClicked: lib.category = modelData.cat
                    }
                }
            }
        }

        /* Type rows + empty states — the state text overlays the
           (empty) list region so the footer keeps its slot. */
        Item {
            width: parent.width
            height: parent.height - y - footer.height - th.sp

        ListView {
            id: libList
            objectName: "libList"
            anchors.fill: parent
            clip: true
            model: lib.shown
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: row
                required property var modelData
                width: libList.width
                height: 54
                radius: th.radiusSm
                color: rowMa.containsMouse ? th.panelAlt : "transparent"

                property string typeId:   modelData.typeId   || ""
                property string rowName:  modelData.name     || typeId
                property string rowCat:   modelData.category || "misc"
                property bool   rowFav:   modelData.favorite === true
                property bool   fromFile: modelData.fromFile === true
                property int    leds:     modelData.ledTotal || 0
                property var    bnd:      modelData.bounds

                function dimsText() {
                    if (!bnd || bnd.x === undefined)
                        return ""
                    var mm = function(v) { return Math.round(v * 1000) }
                    return mm(bnd.x) + "×" + mm(bnd.y) + "×"
                         + mm(bnd.z) + " mm"
                }

                /* Drag chip — a proxy reparented to `lib` so it
                   isn't clipped by the list; carries the type id the
                   scene DropArea reads (drop.source.dropTypeId, with
                   the mimeData key as the fallback path). */
                Item {
                    id: dragChip
                    parent: lib
                    visible: rowMa.drag.active
                    width: chipLbl.implicitWidth + 18
                    height: 24
                    z: 99
                    property string dropTypeId: row.typeId
                    Drag.active: rowMa.drag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["studio-device-type"]
                    Drag.source: dragChip
                    Drag.mimeData: ({ "studio-device-type": row.typeId })
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: height / 2
                    Rectangle {
                        anchors.fill: parent
                        color: th.accentBg; radius: th.radiusSm
                        border.color: th.accent
                        Text {
                            id: chipLbl
                            anchors.centerIn: parent
                            text: row.rowName
                            color: th.text; font.pixelSize: th.fontBody
                        }
                    }
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    hoverEnabled: true
                    drag.target: dragChip
                    drag.axis: Drag.XAndYAxis
                    drag.threshold: 6
                    onPressed: function(m) {
                        /* Park the chip under the cursor so the first
                           drag delta doesn't jump it. */
                        var p = mapToItem(lib, m.x, m.y)
                        dragChip.x = p.x - dragChip.width / 2
                        dragChip.y = p.y - dragChip.height / 2
                    }
                    onReleased: dragChip.Drag.drop()
                }

                Row {
                    x: th.spHalf; y: th.spHalf
                    width: parent.width - th.sp
                    height: parent.height - th.sp
                    spacing: th.spHalf

                    /* Glyph thumbnail — cheap category placeholder,
                       no asset pipeline (task scope). */
                    Rectangle {
                        width: 34; height: 34; radius: th.radiusSm
                        anchors.verticalCenter: parent.verticalCenter
                        color: th.field
                        border.color: th.borderHi
                        Text {
                            anchors.centerIn: parent
                            text: (row.rowCat.charAt(0) || "?").toUpperCase()
                            color: th.accent
                            font.pixelSize: th.fontTitle
                            font.bold: true
                        }
                    }

                    Column {
                        width: parent.width - 34 - starBtn.width
                               - addBtn.width - editBtn.width
                               - th.spHalf * 5
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Text {
                            width: parent.width
                            text: row.rowName
                            color: th.text; font.pixelSize: th.fontBody
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: row.rowCat
                                + (row.dimsText() !== ""
                                   ? " · " + row.dimsText() : "")
                                + " · " + row.leds + " LEDs"
                            color: th.textDim
                            font.pixelSize: th.fontSmall
                            elide: Text.ElideRight
                        }
                        /* Source badge — text + color, never color
                           alone (spec §3). */
                        Text {
                            text: row.fromFile ? "file" : "packaged"
                            color: row.fromFile ? th.accent : th.textFaint
                            font.pixelSize: th.fontSmall
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        IconBtn {
                            id: starBtn
                            text: row.rowFav ? "★" : "☆"
                            active: row.rowFav
                            tip: row.rowFav ? "Remove from favorites"
                                            : "Pin to favorites"
                            onClicked: lib.toggleFav(row.typeId,
                                                     !row.rowFav)
                        }
                        IconBtn {
                            id: addBtn
                            text: "+"
                            tip: "Add to desk (viewport center)"
                            enabled: lib.hasBr()
                            onClicked: lib.addAtCenter(row.typeId)
                        }
                    }
                    IconBtn {
                        id: editBtn
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✎"
                        tip: "Edit type — dimensions, zones, layout, "
                           + "preview"
                        onClicked: lib.editRequested(row.typeId)
                    }
                }
            }
        }

            /* Empty states — empty registry vs. a filter that ate
               everything. */
            Column {
                visible: lib.shown.length === 0
                x: 0; y: 0
                width: parent.width
                spacing: th.spHalf
                Text {
                    text: lib.totalCount === 0
                          ? "No device types found."
                          : "No types match the filter."
                    color: th.textDim; font.pixelSize: th.fontBody
                    width: parent.width
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: lib.totalCount === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Packaged types failed to load — check the "
                        + "diagnostics drawer for preset errors."
                    color: th.textFaint; font.pixelSize: th.fontSmall
                }
            }
        }

        /*================ footer: type actions ================*/
        Column {
            id: footer
            width: parent.width
            spacing: th.spHalf
            Rectangle { width: parent.width; height: 1; color: th.border }
            Text {
                text: {
                    var n = lib.selIds().length
                    return n === 0 ? "select desk instances to save types"
                        : n + " instance" + (n > 1 ? "s" : "")
                          + " selected"
                }
                color: th.textFaint; font.pixelSize: th.fontSmall
            }
            LBtn {
                width: parent.width
                text: "Save variant…"
                tip: "Write the selected instance's type as a new "
                   + "preset and repoint the instance to it"
                enabled: lib.hasBr() && lib.selIds().length === 1
                onClicked: {
                    typeDlg.mode = "variant"
                    idField.text = ""
                    nameField.text = ""
                    typeDlg.open()
                }
            }
            LBtn {
                width: parent.width
                text: "Create preset from selection…"
                tip: "Write a child-reference type from the selected "
                   + "instances' arrangement"
                enabled: lib.hasBr() && lib.selIds().length >= 1
                onClicked: {
                    typeDlg.mode = "assembly"
                    idField.text = ""
                    nameField.text = ""
                    typeDlg.open()
                }
            }
        }
    }

    /* Shared new-type dialog — mode "variant" (repoints the single
       selected instance) or "assembly" (child-ref preset from the
       selection). Live validation above the buttons; the bridge
       re-validates everything anyway. */
    Popup {
        id: typeDlg
        property string mode: "variant"     /* variant | assembly */
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((lib.width - implicitWidth) / 2)
        y: Math.round(lib.height * 0.15)
        padding: th.sp2
        background: Rectangle {
            color: th.panel; radius: th.radius
            border.color: th.borderHi
        }
        onOpened: {
            errText.text = lib.dlgError(idField.text.trim(),
                                        nameField.text.trim())
            idField.forceActiveFocus()
        }

        component DlgBtn: Rectangle {
            property string text: ""
            property alias  ma: dma
            signal clicked()
            width: 72; height: 24; radius: th.radiusSm
            color: dma.containsMouse ? th.panelAlt : th.field
            border.color: dma.activeFocus ? th.accent : th.borderHi
            opacity: enabled ? 1 : 0.4
            Text {
                anchors.centerIn: parent
                text: parent.text
                color: parent.enabled ? th.text : th.textFaint
                font.pixelSize: th.fontBody
            }
            MouseArea {
                id: dma; anchors.fill: parent; hoverEnabled: true
                enabled: parent.enabled
                activeFocusOnTab: true
                onClicked: parent.clicked()
                Keys.onSpacePressed:  parent.clicked()
                Keys.onReturnPressed: parent.clicked()
            }
        }

        contentItem: Column {
            spacing: th.sp
            Text {
                text: typeDlg.mode === "variant"
                      ? "Save variant" : "Create preset from selection"
                color: th.text; font.pixelSize: th.fontTitle
                font.bold: true
            }
            Text {
                text: typeDlg.mode === "variant"
                      ? "Copies the instance's type; the instance is "
                      + "repointed to the new id."
                      : "One child-reference entity per selected "
                      + "instance — the arrangement becomes a type."
                color: th.textDim; font.pixelSize: th.fontSmall
                width: 220; wrapMode: Text.WordWrap
            }
            Text {
                text: "Type id"
                color: th.textDim; font.pixelSize: th.fontSmall
            }
            TextField {
                id: idField
                width: 220; height: 26
                placeholderText: "e.g. fan-120-purple"
                placeholderTextColor: th.textFaint
                color: th.text; font.pixelSize: th.fontBody
                selectByMouse: true
                onTextChanged: errText.text = lib.dlgError(
                    text.trim(), nameField.text.trim())
                onAccepted: if (okBtn.enabled) okBtn.clicked()
                Keys.onShortcutOverride: function(e) {
                    if (e.key === Qt.Key_Escape)
                        e.accepted = true
                }
                Keys.onEscapePressed: typeDlg.close()
                background: Rectangle {
                    color: th.field; radius: th.radiusSm
                    border.color: idField.activeFocus ? th.accent
                                                      : th.borderHi
                }
            }
            Text {
                text: "Display name"
                color: th.textDim; font.pixelSize: th.fontSmall
            }
            TextField {
                id: nameField
                width: 220; height: 26
                placeholderText: "e.g. 120 mm fan (purple)"
                placeholderTextColor: th.textFaint
                color: th.text; font.pixelSize: th.fontBody
                selectByMouse: true
                onTextChanged: errText.text = lib.dlgError(
                    idField.text.trim(), text.trim())
                onAccepted: if (okBtn.enabled) okBtn.clicked()
                Keys.onShortcutOverride: function(e) {
                    if (e.key === Qt.Key_Escape)
                        e.accepted = true
                }
                Keys.onEscapePressed: typeDlg.close()
                background: Rectangle {
                    color: th.field; radius: th.radiusSm
                    border.color: nameField.activeFocus ? th.accent
                                                        : th.borderHi
                }
            }
            Text {
                id: errText
                width: 220; wrapMode: Text.WordWrap
                color: th.bad; font.pixelSize: th.fontSmall
                visible: text !== ""
            }
            Row {
                spacing: th.spHalf
                DlgBtn {
                    id: okBtn
                    text: typeDlg.mode === "variant" ? "Save" : "Create"
                    enabled: errText.text === ""
                    onClicked: {
                        var sel = lib.selIds()
                        lib.submitNewType(typeDlg.mode,
                                          sel.length === 1 ? sel[0] : "",
                                          idField.text.trim(),
                                          nameField.text.trim())
                        lib.stamp++
                        typeDlg.close()
                    }
                }
                DlgBtn {
                    text: "Cancel"
                    onClicked: typeDlg.close()
                }
            }
        }
    }
}
