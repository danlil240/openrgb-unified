/*---------------------------------------------------------*\
||| Inspector.qml                                           ||
|||                                                         ||
|||   Contextual right-hand panel (spec §3). Position and  ||
|||   angle come first — the M2 TransformInspector is      ||
|||   embedded here (one inspector surface, no parallel    ||
|||   implementation); binding/verification, appearance    ||
|||   (paint color), dimensions and instance details live  ||
|||   in expandable sections beneath it.                   ||
|||                                                         ||
|||   In narrow layouts StudioWorkspace shows this panel   ||
|||   as an overlay drawer instead of a docked column.     ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic
import "../editor" as Ed

Rectangle {
    id: insp
    objectName: "inspectorPanel"
    color: th.panel

    property var bridgeOverride: null
    property var hostOverride: null
    property var ctl: null        /* SelectionController (scene) */
    property var cam: null        /* CameraController (scene)    */

    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function host() {
        if (hostOverride !== null)
            return hostOverride
        try { return studioHost } catch (e) { return null }
    }
    function hasBr() { return br() !== null }
    /* Connections targets must be real QObjects — qmltestrunner
       stubs are plain JS values (no objectName property). */
    function brQObj() {
        var b = br()
        return (b !== null && b.objectName !== undefined) ? b : null
    }
    function hostQObj() {
        var h = host()
        return (h !== null && h.objectName !== undefined) ? h : null
    }

    /* True while any numeric/text field inside the panel holds
       focus — StudioScene gates its shortcuts on this through the
       focusPeer seam so typing here never triggers W/E/F/undo. */
    readonly property bool textFocus: inner.textFocus

    /* Primary target = last-selected instance (matches the
       embedded TransformInspector's rule). */
    property int selStamp: 0
    function targetId() {
        selStamp
        var b = br()
        if (!b || b.selectedInstances.length === 0)
            return ""
        return b.selectedInstances[b.selectedInstances.length - 1]
    }

    /* Object rows belonging to the target instance. */
    function rowsOf(inst) {
        var b = br()
        var out = []
        if (!b || inst === "")
            return out
        var list = b.objectList || []
        for (var i = 0; i < list.length; i++) {
            var m = list[i]
            if ((m.instancePath || m.id) === inst)
                out.push(m)
        }
        return out
    }

    /* Old color_btn rule: enabled on a live selection whose object
       isn't decor ("select a light device first" is the C++ fallback
       for the rest). */
    function canPickColor() {
        selStamp
        var b = br()
        if (host() === null || !b || (b.selectedId || "") === "")
            return false
        var info = b.objectInfo ? b.objectInfo(b.selectedId) : {}
        return (info.kind || "") !== "decor"
    }

    /* First emitter-bearing/device row — carries the binding. */
    function bindingRow(inst) {
        var rows = rowsOf(inst)
        for (var i = 0; i < rows.length; i++)
            if (rows[i].kind === "device" || rows[i].kind === "linked")
                return rows[i]
        return rows.length ? rows[0] : null
    }

    Connections {
        target: insp.brQObj()
        function onSelectionChanged() { insp.selStamp++ }
        function onSceneChanged()     { insp.selStamp++ }
    }

    Theme { id: th }

    /* Collapsible section: title row + body. */
    component Section: Column {
        property string title: ""
        property bool open: false
        default property alias content: body.data
        width: parent ? parent.width : 0
        spacing: th.spHalf

        Rectangle {
            width: parent.width; height: 26; radius: th.radiusSm
            color: secHov.containsMouse ? th.panelAlt : "transparent"
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: th.spHalf; spacing: th.spHalf
                Text {
                    text: body.visible ? "▾" : "▸"
                    color: th.textFaint; font.pixelSize: th.fontBody
                }
                Text {
                    text: title
                    color: th.textDim; font.pixelSize: th.fontBody
                }
            }
            MouseArea {
                id: secHov; anchors.fill: parent; hoverEnabled: true
                onClicked: body.visible = !body.visible
            }
        }
        Column {
            id: body
            visible: open
            width: parent.width
            spacing: th.spHalf
            leftPadding: th.sp2
        }
    }

    /* One "key: value" text line. */
    component KV: Text {
        property string k: ""
        property string v: ""
        text: k + ": " + v
        color: th.textDim; font.pixelSize: th.fontSmall
        width: parent ? parent.width : 0
        elide: Text.ElideRight
    }

    Flickable {
        anchors.fill: parent
        contentWidth: col.width
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            width: insp.width
            spacing: th.sp
            padding: th.sp

            /* Header */
            Text {
                text: {
                    insp.selStamp
                    var t = insp.targetId()
                    return t === "" ? "Inspector" : "Inspector — " + t
                }
                color: th.text; font.pixelSize: th.fontTitle; font.bold: true
                elide: Text.ElideRight
                width: col.width - col.padding * 2
            }

            /* Position / angle first — the M2 transform editor. */
            Ed.TransformInspector {
                id: inner
                width: col.width - col.padding * 2
                ctl: insp.ctl
                cam: insp.cam
                bridgeOverride: insp.bridgeOverride
            }

            /* Appearance — paint color + the C++ color dialog. */
            Section {
                title: "Appearance"
                open: true
                Row {
                    spacing: th.sp
                    height: 24
                    Rectangle {
                        width: 40; height: 20; radius: th.radiusSm
                        anchors.verticalCenter: parent.verticalCenter
                        color: insp.hasBr() ? insp.br().paintColor : th.field
                        border.color: th.borderHi
                    }
                    Button {
                        id: colorBtn
                        text: "Color…"
                        height: 24
                        /* The dialog is C++-side (QColorDialog); the
                           seam is studioHost.uiPickColor(). Gated on
                           a non-decor selection like the old bar. */
                        enabled: insp.canPickColor()
                        onClicked: insp.host().uiPickColor()
                        contentItem: Text {
                            text: colorBtn.text
                            color: colorBtn.enabled ? th.text : th.textFaint
                            font.pixelSize: th.fontBody
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: th.radiusSm
                            color: colorBtn.down ? th.accentBg
                                : (colorBtn.hovered ? th.panelAlt : th.field)
                            border.color: colorBtn.visualFocus ? th.accent
                                                               : th.borderHi
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "applies to selection"
                        color: th.textFaint; font.pixelSize: th.fontSmall
                    }
                }
            }

            /* Binding / verification — explicit status text. */
            Section {
                title: "Binding & Output"
                open: true
                Column {
                    width: parent ? parent.width : 0
                    spacing: th.spHalf
                    property var brow: insp.bindingRow(insp.targetId())
                    KV {
                        k: "status"
                        v: {
                            var r = parent.brow
                            if (!r) return "—"
                            var s = th.statusText(r.kind, r.bound)
                            return s === "" ? "—" : s
                        }
                    }
                    KV {
                        k: "emitters"
                        v: {
                            var r = parent.brow
                            return r ? String(r.emitters || 0) : "—"
                        }
                    }
                    KV {
                        k: "verified"
                        v: {
                            var r = parent.brow
                            if (!r) return "—"
                            return r.verified ? "yes (writes on)"
                                              : "no (writes off)"
                        }
                    }
                    KV {
                        k: "reason"
                        v: {
                            var r = parent.brow
                            if (!r || r.bound === "ok" || r.bound === "none"
                                || !insp.hasBr())
                                return "—"
                            var info = insp.br().objectInfo(r.id)
                            return info.reason || "—"
                        }
                    }
                }
            }

            /* Dimensions — authored size_m of the root row. */
            Section {
                title: "Dimensions"
                Column {
                    width: parent ? parent.width : 0
                    spacing: th.spHalf
                    property var rootRow: {
                        var rows = insp.rowsOf(insp.targetId())
                        for (var i = 0; i < rows.length; i++)
                            if (rows[i].id === insp.targetId())
                                return rows[i]
                        return rows.length ? rows[0] : null
                    }
                    KV { k: "size (mm)"
                         v: parent.rootRow
                            ? Math.round(parent.rootRow.dx * 1000) + " × "
                              + Math.round(parent.rootRow.dy * 1000) + " × "
                              + Math.round(parent.rootRow.dz * 1000)
                            : "—" }
                    KV { k: "body (mm)"
                         v: parent.rootRow
                            ? Math.round(parent.rootRow.bx * 1000) + " × "
                              + Math.round(parent.rootRow.by * 1000) + " × "
                              + Math.round(parent.rootRow.bz * 1000)
                            : "—" }
                }
            }

            /* Instance details — type + id (rename lives on the
               device tree row's double-click). */
            Section {
                title: "Instance"
                Column {
                    width: parent ? parent.width : 0
                    spacing: th.spHalf
                    property var st: insp.hasBr() && insp.targetId() !== ""
                                     ? insp.br().instanceState(insp.targetId())
                                     : ({})
                    KV { k: "id";   v: parent.st.id   || "—" }
                    KV { k: "type"; v: parent.st.type || "—" }
                }
            }
        }
    }
}
