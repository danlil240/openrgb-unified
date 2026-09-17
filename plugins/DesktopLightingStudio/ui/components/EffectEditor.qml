/*---------------------------------------------------------*\
|||| EffectEditor.qml                                        ||
||||                                                         ||
||||   Modeless effect-layer editor pane (task 5.2) — the  ||
||||   LookShelf "Edit" affordance opens it over the       ||
||||   workspace like the 4.3 device-preset editor. Left   ||
||||   column is the LayerStack (order/enabled/blend/      ||
||||   opacity/delete + add), right column the             ||
||||   EffectInspector for the selected layer.             ||
||||                                                         ||
||||   Header shows provenance: "inline (customized)" when ||
||||   effects.layers is authored, else the named look.    ||
||||   Footer: Save as look… (presets/effects/<id>.effect. ||
||||   json via bridge.saveLookAs — id==filename,          ||
||||   re-validated; the stack becomes the named look),    ||
||||   Reset to preset (clears the inline stack), Close.   ||
||||                                                         ||
||||   Viewport pick mode: the inspector's armPick() sets  ||
||||   SelectionController.pickMode ("origin"/"path") and  ||
||||   pickTarget — each pick runs begin/drag/end as ONE   ||
||||   effect gesture = one undo command; Escape cancels   ||
||||   and disarms.                                        ||
||||                                                         ||
||||   SPDX-License-Identifier: GPL-2.0-or-later             ||
|\*---------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: fxed
    objectName: "effectEditor"
    visible: false
    color: th.panel
    radius: th.radiusLg
    border.color: th.borderHi
    width: parent ? Math.min(parent.width - 40, 860) : 860
    height: parent ? Math.min(parent.height - 60, 560) : 560
    z: 40

    /* ---------------- seams ---------------- */
    property var bridgeOverride: null
    property var hostOverride: null
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
    function brQObj() {
        var b = br()
        return (b !== null && b.objectName !== undefined) ? b : null
    }

    /* The scene's SelectionController — pick mode target. */
    property var pickCtl: null

    /* Focus-chain member: any focused control inside swallows the
       viewport's W/E/P/F/Ctrl+Z — StudioScene reads this through
       its focusPeer seam (the 4.3 fix). */
    readonly property bool textFocus: {
        var w = fxed.Window
        var f = w ? w.activeFocusItem : null
        while (f) {
            if (f === fxed)
                return true
            f = f.parent
        }
        return false
    }

    /* ---------------- state ---------------- */
    property int selLayer: -1
    property int fxStamp: 0
    property bool saveOpen: false
    property var saveErrors: []
    property string savePath: ""
    /* Open state as data — Item.visible reports EFFECTIVE
       visibility (false under a hidden/zero-size parent in the
       test harness), so callers and tests read this instead. */
    property bool isOpen: false

    function poke() { fxStamp++ }

    function open() {
        visible = true
        isOpen = true
        saveOpen = false
        saveErrors = []
        fxStamp++
        clampSel()
    }
    function close() {
        disarmPick()
        visible = false
        isOpen = false
    }

    /* Escape closes the panel when it (not a field) holds focus —
       the 4.3 DevicePresetEditor idiom: fields eat their own
       Escape first, and closing disarms any armed pick mode. */
    Keys.onEscapePressed: fxed.close()
    onVisibleChanged: if (visible) forceActiveFocus()
    function clampSel() {
        var n = layers.rows().length
        if (selLayer >= n)
            selLayer = n - 1
        if (layers.selIndex !== selLayer)
            layers.selIndex = selLayer
    }
    onSelLayerChanged: {
        /* One-directional push — the stack's own row clicks assign
           its selIndex directly (a binding would die on first
           click). */
        if (layers.selIndex !== selLayer)
            layers.selIndex = selLayer
    }

    Connections {
        target: fxed.brQObj()
        function onEffectLayersChanged() {
            fxed.fxStamp++
            fxed.clampSel()
        }
        function onPresetChanged() { fxed.fxStamp++ }
        function onUndoChanged()   { fxed.fxStamp++ }
    }

    /* ---------------- viewport pick mode ---------------- */
    /* Arm the SelectionController: click/drag in the viewport
       delivers desk-plane points at the layer origin's height.
       One pick = one undoable gesture. */
    function armPick(mode) {
        if (pickCtl === null || pickCtl.pickMode === undefined)
            return
        var l = insp.layerMap()
        if (!l)
            return
        if (pickCtl.pickMode === mode) {
            disarmPick()
            return
        }
        pickCtl.pickPlaneY = l.origin ? l.origin.y : 0
        pickCtl.pickMode = mode
        pickCtl.pickTarget = fxed.onViewportPick
        insp.armedPick = mode
    }
    function disarmPick() {
        if (pickCtl !== null && pickCtl.pickMode !== undefined) {
            pickCtl.pickMode = ""
            pickCtl.pickTarget = null
        }
        insp.armedPick = ""
    }
    /* SelectionController callback: pt is a world point on the
       pick plane; phase begin|drag|end|cancel. */
    function onViewportPick(pt, phase) {
        var b = br()
        if (!b || pickCtl === null)
            return
        var mode = pickCtl.pickMode
        if (phase === "cancel") {
            b.cancelEffectGesture()
            fxed.disarmPick()
            return
        }
        if (phase === "begin") {
            b.beginEffectGesture()
            if (mode === "path")
                b.addEffectLayerPathPoint(selLayer, pt.x, pt.y, pt.z)
            else
                b.setEffectLayerOrigin(selLayer, pt.x, pt.y, pt.z)
            return
        }
        if (phase === "drag") {
            if (mode === "path") {
                var l = insp.layerMap()
                var n = (l && l.path) ? l.path.length : 0
                if (n > 0)
                    b.setEffectLayerPathPoint(selLayer, n - 1,
                                              pt.x, pt.y, pt.z)
            } else {
                b.setEffectLayerOrigin(selLayer, pt.x, pt.y, pt.z)
            }
            return
        }
        /* "end" — re-apply the release point (a click may deliver
           no drag events), then fold the gesture into one record.
           pt is null when the release had no plane hit — skip the
           apply but still close the gesture so it can't leak. */
        if (pt !== null && pt !== undefined) {
            if (mode === "path") {
                var l2 = insp.layerMap()
                var n2 = (l2 && l2.path) ? l2.path.length : 0
                if (n2 > 0)
                    b.setEffectLayerPathPoint(selLayer, n2 - 1,
                                              pt.x, pt.y, pt.z)
            } else {
                b.setEffectLayerOrigin(selLayer, pt.x, pt.y, pt.z)
            }
        }
        b.commitEffectGesture(mode === "path" ? "path waypoint"
                                              : "layer origin")
    }

    /* ---------------- save-as ---------------- */
    function doSave(idText, nameText) {
        saveErrors = []
        var b = br()
        if (!b || typeof b.saveLookAs !== "function") {
            saveErrors = ["bridge has no saveLookAs"]
            return
        }
        var r = b.saveLookAs(idText, nameText)
        if (r && r.ok) {
            savePath = r.path || ""
            saveOpen = false
            fxStamp++
        } else {
            saveErrors = (r && r.errors) ? r.errors
                                         : ["save failed"]
        }
    }

    Theme { id: th }

    component FBtn: Rectangle {
        property string text: ""
        property bool active: false
        property int w: 60
        property string tip: ""
        signal clicked()
        width: w; height: 24; radius: th.radiusSm
        color: active ? th.accentBg
             : (fma.containsMouse ? th.panelAlt : th.field)
        border.color: fma.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
        }
        MouseArea {
            id: fma; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: fma.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    Column {
        anchors.fill: parent
        anchors.margins: th.sp
        spacing: th.spHalf

        /* ---- header: title + provenance ---- */
        Row {
            width: parent.width
            height: 26
            spacing: th.sp
            Text {
                text: "Effect Layers"
                color: th.text; font.pixelSize: th.fontTitle
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                /* Provenance: inline stack = customized; named look
                   = preset id. */
                text: {
                    fxed.fxStamp
                    var b = fxed.br()
                    if (!b)
                        return ""
                    var inline = b.effectStackInline === true
                    var p = b.activePreset || ""
                    return inline ? "inline (customized)"
                         : (p !== "" ? "look: " + p : "empty")
                }
                color: th.textDim; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: parent.width - 340; height: 1 }
            FBtn {
                text: "✕"; w: 24
                tip: "close (Escape in the viewport cancels a pick)"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: fxed.close()
            }
        }

        Rectangle { width: parent.width; height: 1; color: th.border }

        /* ---- body: stack left, inspector right ---- */
        Row {
            width: parent.width
            height: parent.height - 26 - 30 - th.sp * 3
            spacing: th.sp

            Flickable {
                width: 300; height: parent.height
                contentWidth: layers.width
                contentHeight: layers.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                LayerStack {
                    id: layers
                    objectName: "effectEditorStack"
                    width: 300
                    bridgeOverride: fxed.bridgeOverride
                    fxStamp: fxed.fxStamp
                    onLayerSelected: function(idx) {
                        fxed.selLayer = idx
                    }
                }
            }

            Rectangle { width: 1; height: parent.height
                        color: th.border }

            Flickable {
                width: parent.width - 310; height: parent.height
                contentWidth: insp.width
                contentHeight: insp.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                EffectInspector {
                    id: insp
                    objectName: "effectEditorInspector"
                    width: parent.width
                    layerIndex: fxed.selLayer
                    fxStamp: fxed.fxStamp
                    bridgeOverride: fxed.bridgeOverride
                    hostOverride: fxed.hostOverride
                    onArmPick: function(mode) { fxed.armPick(mode) }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: th.border }

        /* ---- footer: save-as + reset ---- */
        Row {
            width: parent.width
            height: 26
            spacing: th.spHalf

            FBtn {
                text: "Save as look…"; w: 104
                tip: "write presets/effects/<id>.effect.json and make it the active look"
                onClicked: {
                    fxed.saveOpen = !fxed.saveOpen
                    fxed.saveErrors = []
                }
            }
            FBtn {
                text: "Reset to preset"; w: 108
                tip: "drop the inline stack — the named look resolves again (undoable)"
                enabled: {
                    fxed.fxStamp
                    return fxed.hasBr()
                           && fxed.br().effectStackInline === true
                }
                onClicked: {
                    var b = fxed.br()
                    if (b) b.resetEffectLayers()
                }
            }
            Item { width: 8; height: 1 }
            Text {
                visible: fxed.savePath !== ""
                text: "saved: " + fxed.savePath
                color: th.ok; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideMiddle
                width: Math.min(implicitWidth, 320)
            }
        }

        /* ---- save-as row (id + name + errors) ---- */
        Row {
            visible: fxed.saveOpen
            width: parent.width
            height: visible ? 26 : 0
            spacing: th.spHalf
            Rectangle {
                width: 130; height: 22; radius: th.radiusSm
                color: th.field
                border.color: idIn.activeFocus ? th.accent : th.borderHi
                TextInput {
                    id: idIn
                    anchors.fill: parent; anchors.margins: 4
                    color: th.text; font.pixelSize: th.fontBody
                    selectByMouse: true
                    validator: RegularExpressionValidator {
                        regularExpression: /[A-Za-z0-9_-]*/
                    }
                    Text {
                        visible: idIn.text === "" && !idIn.activeFocus
                        anchors.verticalCenter: parent.verticalCenter
                        text: "look-id"; color: th.textFaint
                        font.pixelSize: th.fontSmall
                    }
                }
            }
            Rectangle {
                width: 170; height: 22; radius: th.radiusSm
                color: th.field
                border.color: nameIn.activeFocus ? th.accent
                                                 : th.borderHi
                TextInput {
                    id: nameIn
                    anchors.fill: parent; anchors.margins: 4
                    color: th.text; font.pixelSize: th.fontBody
                    selectByMouse: true
                    Text {
                        visible: nameIn.text === "" && !nameIn.activeFocus
                        anchors.verticalCenter: parent.verticalCenter
                        text: "display name (optional)"
                        color: th.textFaint
                        font.pixelSize: th.fontSmall
                    }
                }
            }
            FBtn {
                text: "Save"; w: 46
                enabled: idIn.text !== ""
                onClicked: fxed.doSave(idIn.text, nameIn.text)
            }
            FBtn {
                text: "Cancel"; w: 54
                onClicked: { fxed.saveOpen = false; fxed.saveErrors = [] }
            }
            Text {
                visible: fxed.saveErrors.length > 0
                text: fxed.saveErrors.join("  ")
                color: th.bad; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 300)
            }
        }
    }
}
