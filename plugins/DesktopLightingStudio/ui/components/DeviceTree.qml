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
    property int rowCount: 0

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

    Connections {
        target: treeRoot.brQObj()
        function onSelectionChanged() { treeRoot.selStamp++ }
        function onSceneChanged()     { treeRoot.selStamp++ }
    }

    /* Compact icon button — text glyphs keep this theme-free. */
    component IconBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property string tip: ""
        signal clicked()
        width: 26; height: 22; radius: th.radiusSm
        color: active ? th.accentBg
                      : (hov.containsMouse ? th.panelAlt : th.field)
        border.color: active ? th.accent : th.borderHi
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontSmall
        }
        MouseArea {
            id: hov; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            onClicked: parent.clicked()
        }
        ToolTip.visible: hov.containsMouse && tip !== ""
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
                text: "Del"; tip: "Delete selected"
                enabled: treeRoot.selCount() >= 1
                opacity: enabled ? 1 : 0.4
                onClicked: if (treeRoot.hasBr()) treeRoot.br().deleteSelected()
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
                    onClicked: function(m) {
                        row.activate(m.modifiers)
                    }
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
                        onClicked: if (treeRoot.hasBr())
                            treeRoot.br().setInstanceVisible(row.rowInst,
                                                             !row.rowVis)
                    }
                    IconBtn {
                        id: lockBtn
                        text: "L"
                        active: row.rowLock
                        tip: row.rowLock ? "Unlock instance" : "Lock instance"
                        onClicked: if (treeRoot.hasBr())
                            treeRoot.br().setInstanceLocked(row.rowInst,
                                                            !row.rowLock)
                    }
                }
            }
        }

        /* Empty state + first-run guidance (spec §3): choose a
           layout → match devices → arrange → choose a look. */
        Column {
            visible: treeList.count === 0
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
}
