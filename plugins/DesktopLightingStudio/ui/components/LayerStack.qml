/*---------------------------------------------------------*\
|||| LayerStack.qml                                          ||
||||                                                         ||
||||   Ordered effect-layer list (task 5.2): one row per   ||
||||   layer — enable toggle, primitive label, blend       ||
||||   cycle, opacity slider, required-input badge,        ||
||||   reorder/delete buttons. Rows come from the          ||
||||   bridge's EffectLayerModel (rowAt/count) or the      ||
||||   effectLayerCount/effectLayer invokables, so a       ||
||||   plain-JS stub bridge drives the same code in the    ||
||||   qmltestrunner suite.                                ||
||||                                                         ||
||||   Undo contract: every button/toggle lands one        ||
||||   record through the bridge invokables; the opacity   ||
||||   slider wraps its scrub in begin/commitEffectGesture ||
||||   so a drag is exactly one command.                   ||
||||                                                         ||
||||   SPDX-License-Identifier: GPL-2.0-or-later             ||
|\*---------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: stack
    objectName: "layerStack"
    color: th.panel

    /* ---------------- seams ---------------- */
    property var bridgeOverride: null
    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function hasBr() { return br() !== null }
    function brQObj() {
        var b = br()
        return (b !== null && b.objectName !== undefined) ? b : null
    }

    /* Selected row — the effect inspector follows this. */
    property int selIndex: -1
    signal layerSelected(int idx)

    /* Re-eval stamp — bumped by the bridge's effectLayersChanged /
       undoChanged when a real bridge is attached; tests bump it. */
    property int fxStamp: 0
    function poke() { fxStamp++ }

    Connections {
        target: stack.brQObj()
        function onEffectLayersChanged() { stack.fxStamp++ }
        function onUndoChanged()         { stack.fxStamp++ }
    }

    /* Normalized row list. The real bridge hands over an
       EffectLayerModel (count()/rowAt()); a stub may supply a plain
       JS array under the same property; last resort is the
       effectLayerCount/effectLayer invokable pair. */
    function rows() {
        fxStamp
        var b = br()
        if (!b)
            return []
        var m = b.effectLayerModel
        if (m !== undefined && m !== null) {
            if (m.length !== undefined)
                return m                        /* JS-array stub */
            var n = (typeof m.count === "function") ? m.count() : m.count
            var out = []
            for (var r = 0; r < n; r++)
                out.push(m.rowAt(r))
            return out
        }
        var n2 = (typeof b.effectLayerCount === "function")
               ? b.effectLayerCount() : 0
        var rows = []
        for (var i = 0; i < n2; i++) {
            var l = b.effectLayer(i) || {}
            rows.push({
                index: i,
                primitive: l.primitive || "",
                enabled: l.enabled !== false,
                blend: l.blend || "replace",
                opacity: (l.opacity === undefined) ? 1 : l.opacity,
                space: l.space || "world",
                source: l.source || "",
                stops: (l.palette || []).length,
                pathPts: (l.path || []).length,
                summary: ""
            })
        }
        return rows
    }

    /* Required-input badge for a layer's `source` —
       {text, state:""|"ok"|"off"|"down"}. Icon glyph + words:
       never color alone (spec §3). */
    function srcBadge(src) {
        if (!src)
            return { text: "", state: "" }
        var b = br()
        if (!b || typeof b.inputSourceState !== "function")
            return { text: src, state: "" }
        var st = b.inputSourceState(src)
        if (!st || st.name === undefined)
            return { text: src, state: "" }
        if (!st.enabled)
            return { text: "needs " + src + " — off", state: "off" }
        if (!st.ready)
            return { text: "needs " + src + " — "
                     + (st.status || "disconnected"), state: "down" }
        return { text: src, state: "ok" }
    }

    function blendNext(b) {
        return b === "replace" ? "add" : b === "add" ? "screen" : "replace"
    }

    /* ---- row actions — the delegates call these; tests drive
       them directly (one undoable bridge call each). ---- */
    function toggleEnabled(row) {
        var b = br()
        if (b) b.setEffectLayerEnabled(row.index, !row.enabled)
    }
    function cycleBlend(row) {
        var b = br()
        if (b) b.setEffectLayerBlend(row.index, blendNext(row.blend))
    }
    function moveRow(from, to) {
        var b = br()
        if (b) b.moveEffectLayer(from, to)
    }
    function removeRow(i) {
        var b = br()
        if (b) b.removeEffectLayer(i)
    }
    function addLayer(prim) {
        var b = br()
        if (!b)
            return
        b.addEffectLayer(prim)
        selIndex = rows().length - 1
        layerSelected(selIndex)
    }
    /* Opacity scrub = one effect gesture = one undo record. */
    function opScrubBegin() {
        var b = br()
        if (b) b.beginEffectGesture()
    }
    function opScrubMove(i, v) {
        var b = br()
        if (b) b.setEffectLayerOpacity(i, v)
    }
    function opScrubEnd() {
        var b = br()
        if (b) b.commitEffectGesture("layer opacity")
    }

    implicitHeight: col.implicitHeight

    Theme { id: th }

    component MiniBtn: Rectangle {
        property string text: ""
        property int w: 22
        property string tip: ""
        signal clicked()
        width: w; height: 22; radius: th.radiusSm
        color: mma.containsMouse ? th.panelAlt : th.field
        border.color: mma.activeFocus ? th.accent : th.borderHi
        opacity: enabled ? 1 : 0.35
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontSmall
        }
        MouseArea {
            id: mma; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: mma.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    Column {
        id: col
        width: stack.width
        spacing: 2

        Repeater {
            id: lv
            model: stack.rows()
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                width: col.width
                height: 44
                radius: th.radiusSm
                color: stack.selIndex === row.modelData.index
                       ? th.selRow
                       : (rowMa.containsMouse ? th.panelAlt : "transparent")
                border.color: stack.selIndex === row.modelData.index
                              ? th.accent : "transparent"

                /* Row click selects — the inspector follows. */
                MouseArea {
                    id: rowMa; anchors.fill: parent; hoverEnabled: true
                    onClicked: {
                        stack.selIndex = row.modelData.index
                        stack.layerSelected(row.modelData.index)
                    }
                }

                /* enable checkbox */
                Rectangle {
                    id: ena
                    x: 6; width: 15; height: 15; radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: row.modelData.enabled ? th.accentBg : th.field
                    border.color: row.modelData.enabled ? th.accent
                                                        : th.borderHi
                    Text {
                        anchors.centerIn: parent
                        visible: row.modelData.enabled
                        text: "✓"; color: th.text
                        font.pixelSize: th.fontSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: stack.toggleEnabled(row.modelData)
                    }
                }

                /* primitive + summary + input badge */
                Column {
                    x: 28; width: parent.width - 218
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Text {
                        text: row.modelData.primitive
                        color: row.modelData.enabled ? th.text : th.textDim
                        font.pixelSize: th.fontBody
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: {
                            stack.fxStamp
                            var s = row.modelData.summary || ""
                            var badge = stack.srcBadge(row.modelData.source)
                            var btxt = badge.text === "" ? ""
                                     : (s === "" ? badge.text
                                                 : " · " + badge.text)
                            return (row.modelData.space || "world")
                                   + (s === "" ? "" : " · " + s) + btxt
                        }
                        color: {
                            stack.fxStamp
                            var badge = stack.srcBadge(row.modelData.source)
                            if (badge.state === "off"
                                || badge.state === "down")
                                return th.warn
                            return th.textFaint
                        }
                        font.pixelSize: th.fontSmall
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                /* blend cycle */
                MiniBtn {
                    id: blendBtn
                    x: parent.width - 188; w: 56
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.blend
                    tip: "blend — click cycles replace/add/screen"
                    onClicked: stack.cycleBlend(row.modelData)
                }

                /* opacity — gesture-wrapped scrub: one undo record. */
                Slider {
                    id: opSl
                    x: parent.width - 128; width: 66
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0; to: 1
                    value: row.modelData.opacity
                    onPressedChanged: {
                        if (pressed)
                            stack.opScrubBegin()
                        else
                            stack.opScrubEnd()
                    }
                    onMoved: stack.opScrubMove(row.modelData.index, value)
                    implicitHeight: 18
                    background: Rectangle {
                        x: opSl.leftPadding
                        y: opSl.topPadding + opSl.availableHeight / 2 - 2
                        width: opSl.availableWidth; height: 4
                        radius: 2; color: th.field
                        Rectangle {
                            width: opSl.visualPosition * parent.width
                            height: parent.height; radius: 2
                            color: th.accentBg
                        }
                    }
                    handle: Rectangle {
                        x: opSl.leftPadding + opSl.visualPosition
                           * (opSl.availableWidth - width)
                        y: opSl.topPadding + opSl.availableHeight / 2
                           - height / 2
                        width: 12; height: 12; radius: 6
                        color: opSl.pressed ? th.accent : th.textDim
                        border.color: th.border
                    }
                }

                MiniBtn {
                    x: parent.width - 58; w: 18; text: "↑"
                    tip: "move earlier (drawn first)"
                    enabled: row.modelData.index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: stack.moveRow(row.modelData.index,
                                             row.modelData.index - 1)
                }
                MiniBtn {
                    x: parent.width - 39; w: 18; text: "↓"
                    tip: "move later (drawn on top)"
                    enabled: row.modelData.index < lv.count - 1
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: stack.moveRow(row.modelData.index,
                                             row.modelData.index + 1)
                }
                MiniBtn {
                    x: parent.width - 20; w: 18; text: "✕"
                    tip: "remove layer"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: stack.removeRow(row.modelData.index)
                }
            }
        }

        /* Empty state — an inline-cleared workspace or a fresh
           scene. */
        Text {
            visible: lv.count === 0
            text: "no layers — pick a look or add one below"
            color: th.textFaint; font.pixelSize: th.fontSmall
            leftPadding: th.spHalf
            height: 22; verticalAlignment: Text.AlignVCenter
        }

        /* Add-layer row: primitive picker + button. */
        Row {
            spacing: th.spHalf
            height: 26
            leftPadding: 2

            ComboBox {
                id: primBox
                height: 24; width: 118
                anchors.verticalCenter: parent.verticalCenter
                model: [ "static", "gradient", "wave", "pulse",
                         "comet", "noise", "spin", "ripple",
                         "screenfield", "level" ]
                contentItem: Text {
                    text: primBox.displayText
                    color: th.text; font.pixelSize: th.fontBody
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                background: Rectangle {
                    radius: th.radiusSm; color: th.field
                    border.color: primBox.visualFocus ? th.accent
                                                      : th.borderHi
                }
            }
            MiniBtn {
                text: "+ add layer"; w: 78
                tip: "append a layer (one undoable edit)"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: stack.addLayer(primBox.currentText)
            }
        }
    }
}
