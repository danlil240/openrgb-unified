/*---------------------------------------------------------*\
||| DeviceTree.qml                                          ||
|||                                                         ||
|||   Searchable instance hierarchy for the workspace     ||
|||   shell. Driven by the bridge's stable                ||
|||   QAbstractListModel (objectModel); the legacy         ||
|||   objectList QVariantList is the stub/test fallback.   ||
|||   Rows indent by object-id depth (instance = root,     ||
|||   entities nest under it). Each row shows selection,   ||
|||   visibility + lock toggles, and an explicit           ||
|||   Connected/Missing/Unmapped/Mirrored status chip —    ||
|||   text, never color alone (spec §3).                   ||
|||                                                         ||
|||   Click selects like a viewport click (object id via   ||
|||   bridge.select); Ctrl+click is additive on the        ||
|||   instance. Double-click a root row renames the        ||
|||   instance. Group/ungroup/delete/mirrored-duplicate    ||
|||   live in the toolbar — the bridge slots already       ||
|||   existed but had no UI surface.                       ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: treeRoot
    objectName: "deviceTree"
    color: th.panel

    /* Same seam as SelectionController: the plugin supplies the
       `bridge` context property; qmltestrunner injects a stub. */
    property var bridgeOverride: null
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

    /* Re-eval stamps: the stub path has no NOTIFY, and rows also
       re-read instance flags after toggles. */
    property int selStamp: 0
    function pokeSel() { selStamp++ }

    property string search: ""
    /* Rows currently matching the search — recomputed by
       treeList.recount() on model/search/delegate changes (the
       model count never shrinks under filtering). */
    property int shownCount: 0
    onSearchChanged: Qt.callLater(treeList.recount)

    Theme { id: th }

    function modelData() {
        var b = br()
        if (!b)
            return []
        if (b.objectModel)
            return b.objectModel
        if (b.objectList)
            return b.objectList
        return []
    }

    function selCount() {
        selStamp
        var b = br()
        return b ? b.selectedInstances.length : 0
    }

    /* Spec §4: deleting a group previews the children it takes
       with it. The bridge computes the exact kill set Delete would
       use; only a cascade (kill > selection) earns the prompt —
       plain deletes stay one click, undo is the backstop. */
    function requestDelete() {
        if (!hasBr())
            return
        var b = br()
        if (typeof b.deletePreview !== "function") {
            b.deleteSelected()          /* stub/test path */
            return
        }
        var kill = b.deletePreview()
        var sel = b.selectedInstances || []
        if (kill.length <= sel.length) {
            /* Empty kill set: let deleteSelected run so a locked-
               selection refusal still surfaces on the status line. */
            b.deleteSelected()
            return
        }
        delConfirm.selSet = sel.slice()
        delConfirm.killSet = kill
        delConfirm.open()
    }

    Connections {
        target: treeRoot.brQObj()
        function onSelectionChanged() { treeRoot.selStamp++ }
        function onSceneChanged()     { treeRoot.selStamp++ }
    }

    /* Compact icon button — text glyphs keep this theme-free.
       `focusable: false` opts per-row copies out of Tab order (the
       row itself is the stop — a dozen devices would otherwise
       double the tab chain). */
    component IconBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property string tip: ""
        property bool   focusable: true
        signal clicked()
        width: 26; height: 22; radius: th.radiusSm
        color: active ? th.accentBg
                      : (hov.containsMouse ? th.panelAlt : th.field)
        border.color: hov.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontSmall
        }
        MouseArea {
            id: hov; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: parent.focusable
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: (hov.containsMouse || hov.activeFocus)
                         && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    Column {
        anchors.fill: parent
        anchors.margins: th.sp
        spacing: th.sp

        /* Header + instance ops */
        Row {
            width: parent.width
            spacing: th.spHalf
            Text {
                text: "Devices"
                color: th.text; font.pixelSize: th.fontTitle; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: 1; height: 1 }
            IconBtn {
                text: "Grp"; tip: "Group selected instances"
                enabled: treeRoot.selCount() >= 2
                opacity: enabled ? 1 : 0.4
                onClicked: if (treeRoot.hasBr()) treeRoot.br().groupSelected()
            }
            IconBtn {
                text: "Un"; tip: "Ungroup selected"
                enabled: treeRoot.selCount() >= 1
                opacity: enabled ? 1 : 0.4
                onClicked: if (treeRoot.hasBr()) treeRoot.br().ungroupSelected()
            }
            IconBtn {
                text: "Mir"; tip: "Duplicate mirrored (shares output)"
                enabled: treeRoot.selCount() >= 1
                opacity: enabled ? 1 : 0.4
                onClicked: if (treeRoot.hasBr()) treeRoot.br().duplicateMirrored()
            }
            IconBtn {
                text: "Del"; tip: "Delete selected (groups ask first)"
                enabled: treeRoot.selCount() >= 1
                opacity: enabled ? 1 : 0.4
                onClicked: treeRoot.requestDelete()
            }
        }

        /* Search */
        TextField {
            id: searchField
            width: parent.width
            height: 28
            placeholderText: "Search devices…"
            placeholderTextColor: th.textFaint
            color: th.text
            font.pixelSize: th.fontBody
            activeFocusOnTab: true
            onTextChanged: treeRoot.search = text
            /* Escape cancels the FIELD (clear, then drop focus) —
               swallowing the ShortcutOverride keeps it away from
               the scene's Escape -> clearEditorSelection. */
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
                border.color: searchField.activeFocus ? th.accent : th.borderHi
            }
        }

        /* Instance tree — flat model, indented by id depth. */
        ListView {
            id: treeList
            objectName: "treeList"
            width: parent.width
            height: parent.height - y
            clip: true
            model: treeRoot.modelData()
            boundsBehavior: Flickable.StopAtBounds

            /* Tally shown delegates — a +1/-1 running count drifts
               (a bound `shown` can fire onShownChanged at creation),
               so recount after any change via callLater. */
            function recount() {
                var n = 0
                for (var i = 0; i < count; i++) {
                    var it = itemAtIndex(i)
                    if (it && it.shown)
                        n++
                }
                treeRoot.shownCount = n
            }
            onCountChanged: Qt.callLater(recount)
            onModelChanged: Qt.callLater(recount)

            delegate: Rectangle {
                id: row
                width: treeList.width
                /* Field reader — model roles first, modelData (stub
                   list path) second; mirrors StudioScene's rf(). */
                function rf(name, dflt) {
                    var v = model[name]
                    if (v === undefined && typeof modelData !== "undefined")
                        v = modelData[name]
                    return v === undefined ? dflt : v
                }
                property string rowId:    rf("id", "")
                property string rowInst:  rf("instancePath", rowId)
                property string rowLabel: rf("label", rowId)
                property string rowKind:  rf("kind", "decor")
                property string rowBound: rf("bound", "none")
                property bool   rowVis:   rf("visible", true)
                property bool   rowLock:  rf("locked", false)
                property int    depth:    rowId === "" ? 0
                                           : rowId.split("/").length - 1
                property bool   renaming: false

                /* Row activation shared by the MouseArea and tests. */
                function activate(modifiers) {
                    if (!treeRoot.hasBr())
                        return
                    if (modifiers & Qt.ControlModifier)
                        treeRoot.br().selectInstance(row.rowInst, true)
                    else
                        treeRoot.br().select(row.rowId)
                    treeRoot.selStamp++
                }

                function matches() {
                    var s = treeRoot.search.toLowerCase()
                    if (s === "")
                        return true
                    return rowId.toLowerCase().indexOf(s) >= 0
                        || rowLabel.toLowerCase().indexOf(s) >= 0
                }
                property bool shown: matches()
                height: shown ? 28 : 0
                visible: shown
                onShownChanged:          Qt.callLater(treeList.recount)
                Component.onDestruction: Qt.callLater(treeList.recount)
                radius: th.radiusSm
                color: sel ? th.selRow : (rowMouse.containsMouse ? th.panelAlt
                                                           : "transparent")
                border.color: rowMouse.activeFocus ? th.accent
                                                   : "transparent"

                property bool sel: {
                    treeRoot.selStamp
                    var b = treeRoot.br()
                    return b ? b.selectedInstances.indexOf(rowInst) >= 0
                             : false
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    activeFocusOnTab: true   /* rows are tab stops */
                    onClicked: function(m) {
                        row.activate(m.modifiers)
                    }
                    Keys.onSpacePressed:  row.activate(0)
                    Keys.onReturnPressed: row.activate(0)
                    onDoubleClicked: function(m) {
                        /* Inline rename on root rows only — ids map
                           1:1 to instance ids there. */
                        if (row.depth === 0) {
                            row.renaming = true
                            renameEdit.forceActiveFocus()
                        }
                    }
                }

                Row {
                    x: th.sp + row.depth * 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - th.sp
                    spacing: 6

                    Text {
                        visible: !row.renaming
                        width: parent.width - chip.width - eyeBtn.width
                               - lockBtn.width - 18
                        text: row.rowLabel !== "" ? row.rowLabel : row.rowId
                        color: row.rowVis ? th.text : th.textFaint
                        font.pixelSize: th.fontBody
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextInput {
                        id: renameEdit
                        visible: row.renaming
                        width: parent.width - chip.width - eyeBtn.width
                               - lockBtn.width - 18
                        text: row.rowInst
                        color: th.text; font.pixelSize: th.fontBody
                        selectByMouse: true
                        /* Escape cancels the RENAME — swallow the
                           ShortcutOverride so the scene's Escape ->
                           clearEditorSelection can't fire through. */
                        Keys.onShortcutOverride: function(e) {
                            if (e.key === Qt.Key_Escape)
                                e.accepted = true
                        }
                        Keys.onEscapePressed: {
                            row.renaming = false
                            renameEdit.focus = false
                        }
                        onAccepted: {
                            if (treeRoot.hasBr() && text !== row.rowInst)
                                treeRoot.br().renameInstance(row.rowInst, text)
                            row.renaming = false
                        }
                        onActiveFocusChanged:
                            if (!activeFocus && row.renaming)
                                row.renaming = false
                    }

                    /* Status chip — text + color (spec §3). */
                    Rectangle {
                        id: chip
                        visible: chipText.text !== ""
                        width: chipText.width + 10
                        height: 16
                        radius: th.radiusSm
                        color: "transparent"
                        border.color: th.statusColor(row.rowKind, row.rowBound)
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: th.statusText(row.rowKind, row.rowBound)
                            color: th.statusColor(row.rowKind, row.rowBound)
                            font.pixelSize: th.fontSmall
                        }
                    }

                    IconBtn {
                        id: eyeBtn
                        text: row.rowVis ? "●" : "○"
                        tip: row.rowVis ? "Hide instance" : "Show instance"
                        focusable: false   /* the row owns the tab stop */
                        onClicked: if (treeRoot.hasBr())
                            treeRoot.br().setInstanceVisible(row.rowInst,
                                                             !row.rowVis)
                    }
                    IconBtn {
                        id: lockBtn
                        text: "L"
                        active: row.rowLock
                        tip: row.rowLock ? "Unlock instance" : "Lock instance"
                        focusable: false
                        onClicked: if (treeRoot.hasBr())
                            treeRoot.br().setInstanceLocked(row.rowInst,
                                                            !row.rowLock)
                    }
                }
            }
        }

        /* Empty state + first-run guidance (spec §3): choose a
           layout → match devices → arrange → choose a look. Gated
           on shownCount — filtering collapses rows without changing
           the model count, so a no-match search lands here too. */
        Column {
            visible: treeRoot.shownCount === 0
            width: parent.width
            spacing: th.spHalf
            Text {
                text: treeRoot.search !== ""
                      ? "No devices match the search."
                      : "The desk is empty."
                color: th.textDim; font.pixelSize: th.fontBody
            }
            Text {
                visible: treeRoot.search === ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: "Getting started: choose a desk layout → " +
                      "match devices → arrange them → pick a look below."
                color: th.textFaint; font.pixelSize: th.fontSmall
            }
        }
    }

    /* Spec §4 delete preview — when the selection's kill set is
       larger than the selection itself (group cascade), list the
       children before committing. Undo remains the backstop. */
    Popup {
        id: delConfirm
        property var killSet: []
        property var selSet:  []
        /* killSet minus selSet = the children that ride along. */
        property var childIds: {
            var out = []
            for (var i = 0; i < killSet.length; i++)
                if (selSet.indexOf(killSet[i]) < 0)
                    out.push(killSet[i])
            return out
        }

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.round((treeRoot.width - width) / 2)
        y: Math.round((treeRoot.height - implicitHeight) / 2)
        padding: th.sp2
        background: Rectangle {
            color: th.panel; radius: th.radius
            border.color: th.borderHi
        }
        onOpened: delYesBtn.ma.forceActiveFocus()

        component DlgBtn: Rectangle {
            property string text: ""
            property bool   danger: false
            /* The focusable surface — the popup primes it. */
            property alias  ma: dma
            signal clicked()
            width: 64; height: 24; radius: th.radiusSm
            color: danger ? (dma.containsMouse ? th.accentBg : th.field)
                          : (dma.containsMouse ? th.panelAlt : th.field)
            border.color: dma.activeFocus ? th.accent : th.borderHi
            Text {
                anchors.centerIn: parent
                text: parent.text
                color: parent.enabled ? th.text : th.textFaint
                font.pixelSize: th.fontBody
            }
            MouseArea {
                id: dma; anchors.fill: parent; hoverEnabled: true
                activeFocusOnTab: true
                onClicked: parent.clicked()
                Keys.onSpacePressed:  parent.clicked()
                Keys.onReturnPressed: parent.clicked()
            }
        }

        contentItem: Column {
            spacing: th.sp
            Text {
                text: delConfirm.selSet.length <= 1
                      ? "Delete '" + (delConfirm.selSet[0] || "") + "' + "
                        + delConfirm.childIds.length + " "
                        + (delConfirm.childIds.length === 1
                           ? "child" : "children") + "?"
                      : "Delete " + delConfirm.selSet.length
                        + " instances + " + delConfirm.childIds.length
                        + " children?"
                color: th.text; font.pixelSize: th.fontBody
                font.bold: true
            }
            Text {
                /* The kill set — ids beyond the selection itself. */
                property int max: 8
                width: 260
                wrapMode: Text.WrapAnywhere
                text: delConfirm.childIds.slice(0, max).join(", ")
                      + (delConfirm.childIds.length > max
                         ? "  +" + (delConfirm.childIds.length - max)
                           + " more" : "")
                color: th.textDim; font.pixelSize: th.fontSmall
            }
            Row {
                spacing: th.sp
                layoutDirection: Qt.RightToLeft
                anchors.right: parent.right
                DlgBtn {
                    id: delYesBtn
                    text: "Delete"; danger: true
                    onClicked: {
                        delConfirm.close()
                        if (treeRoot.hasBr())
                            treeRoot.br().deleteSelected()
                    }
                }
                DlgBtn {
                    text: "Cancel"
                    onClicked: delConfirm.close()
                }
            }
        }
    }
}
