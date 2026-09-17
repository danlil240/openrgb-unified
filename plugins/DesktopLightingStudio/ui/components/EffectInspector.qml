/*---------------------------------------------------------*\
|||| EffectInspector.qml                                     ||
||||                                                         ||
||||   Per-layer field inspector (task 5.2): space,        ||
||||   origin/direction XYZ (mm display, m storage — the   ||
||||   transform inspector's unit rule), speed/scale/phase/ ||
||||   density/seed numeric commits, source selector,      ||
||||   targets token editor, embedded PaletteEditor and    ||
||||   the comet path list. A per-primitive relevance      ||
||||   table (fieldsFor) hides fields the engine never     ||
||||   reads for the active primitive — no free-for-all.   ||
||||                                                         ||
||||   Undo contract: each field commit is one undoable    ||
||||   bridge edit (setEffectLayerField / Origin /         ||
||||   Direction / Source / Targets). The viewport Pick    ||
||||   buttons emit armPick — EffectEditor wires that to   ||
||||   the scene's SelectionController pick mode, whose    ||
||||   click/drag lands as ONE effect gesture.             ||
||||                                                         ||
||||   SPDX-License-Identifier: GPL-2.0-or-later             ||
|\*---------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: einsp
    objectName: "effectInspector"
    color: th.panel

    /* ---------------- seams ---------------- */
    property var bridgeOverride: null
    property var hostOverride: null
    function br() {
        if (bridgeOverride !== null)
            return bridgeOverride
        try { return bridge } catch (e) { return null }
    }
    function hasBr() { return br() !== null }

    property int layerIndex: -1
    property int fxStamp: 0
    function poke() { fxStamp++ }

    /* Viewport pick arming — EffectEditor owns the
       SelectionController; we only ask. mode: "origin" | "path". */
    signal armPick(string mode)
    /* While armed (reported back by the editor) the pick buttons
       light up — tests drive armedPick directly. */
    property string armedPick: ""

    /* The selected layer as a plain map (bridge.effectLayer(i)):
       primitive/enabled/blend/space/opacity/speed/scale/phase/
       density/seed/source/origin{xyz}/direction{xyz}/path[]/
       palette[]/targets[] — null when out of range. */
    /* (named layerMap — "layer" is Item's attached property) */
    function layerMap() {
        fxStamp
        var b = br()
        if (!b || layerIndex < 0
            || typeof b.effectLayer !== "function")
            return null
        var l = b.effectLayer(layerIndex)
        return (l && l.primitive !== undefined) ? l : null
    }

    /* Field-relevance per primitive — derived from the evaluators
       in effects/EffectTypes.cpp: a field only shows when the
       engine actually reads it for this primitive. `needs` is the
       implicit input the primitive consumes even with no source
       string (screenfield -> screen, level -> audio). */
    function fieldsFor(prim) {
        switch (prim) {
        case "gradient":
            return { space: 1, direction: 1, speed: 1, scale: 1,
                     phase: 1, palette: 1, targets: 1 }
        case "wave":
            return { space: 1, direction: 1, speed: 1, scale: 1,
                     phase: 1, density: 1, palette: 1, targets: 1 }
        case "pulse":
            return { space: 1, origin: 1, speed: 1, scale: 1,
                     phase: 1, density: 1, palette: 1, targets: 1 }
        case "comet":
            return { space: 1, path: 1, speed: 1, scale: 1,
                     phase: 1, palette: 1, targets: 1 }
        case "noise":
            return { space: 1, direction: 1, speed: 1, scale: 1,
                     density: 1, seed: 1, palette: 1, targets: 1 }
        case "spin":
            return { space: 1, origin: 1, speed: 1, scale: 1,
                     phase: 1, palette: 1, targets: 1 }
        case "ripple":
            return { space: 1, origin: 1, speed: 1, scale: 1,
                     density: 1, source: 1, palette: 1, targets: 1 }
        case "screenfield":
            return { space: 1, origin: 1, scale: 1,
                     needs: "screen", targets: 1 }
        case "level":
            return { needs: "audio", palette: 1, targets: 1 }
        default:    /* static + anything unknown */
            return { palette: 1, targets: 1 }
        }
    }

    /* Implicit-input badge for needs-only primitives — same
       disconnected semantics as LayerStack.srcBadge. */
    function needsBadge(needs) {
        if (!needs)
            return { text: "", state: "" }
        var b = br()
        if (!b || typeof b.inputSourceState !== "function")
            return { text: "needs " + needs, state: "" }
        var st = b.inputSourceState(needs)
        if (!st || st.name === undefined)
            return { text: "needs " + needs, state: "" }
        if (!st.enabled)
            return { text: "needs " + needs + " — off", state: "off" }
        if (!st.ready)
            return { text: "needs " + needs + " — "
                     + (st.status || "disconnected"), state: "down" }
        return { text: needs + " live", state: "ok" }
    }

    function commitField(name, v) {
        var b = br()
        if (b) b.setEffectLayerField(layerIndex, name, v)
    }
    function commitVec(kind, axis, mm) {
        /* origin/direction rows: read current, replace one axis,
           commit the triple. mm for origin (m storage); raw for
           direction. */
        var l = layerMap()
        if (!l)
            return
        var b = br()
        if (!b)
            return
        var cur = (kind === "origin") ? l.origin : l.direction
        var scale = (kind === "origin") ? 1000.0 : 1.0
        var x = cur.x, y = cur.y, z = cur.z
        if (axis === 0)      x = mm / scale
        else if (axis === 1) y = mm / scale
        else                 z = mm / scale
        if (kind === "origin")
            b.setEffectLayerOrigin(layerIndex, x, y, z)
        else
            b.setEffectLayerDirection(layerIndex, x, y, z)
    }
    function commitPath(pt, axis, mm) {
        var l = layerMap()
        if (!l || pt < 0 || pt >= l.path.length)
            return
        var b = br()
        if (!b)
            return
        var p = l.path[pt]
        var x = p.x, y = p.y, z = p.z
        if (axis === 0)      x = mm / 1000.0
        else if (axis === 1) y = mm / 1000.0
        else                 z = mm / 1000.0
        b.setEffectLayerPathPoint(layerIndex, pt, x, y, z)
    }
    function commitTargets(list) {
        var b = br()
        if (b) b.setEffectLayerTargets(layerIndex, list)
    }
    function removeTarget(t) {
        var l = layerMap()
        if (!l)
            return
        var out = []
        for (var i = 0; i < l.targets.length; i++)
            if (l.targets[i] !== t)
                out.push(l.targets[i])
        commitTargets(out)
    }
    function addTarget(t) {
        t = (t || "").trim()
        if (t === "")
            return
        var l = layerMap()
        if (!l)
            return
        if (l.targets.indexOf(t) >= 0)
            return
        var out = l.targets.slice()
        out.push(t)
        commitTargets(out)
    }

    implicitHeight: col.implicitHeight

    Theme { id: th }

    /* Small label button (same idiom as the transform inspector). */
    component TBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property int    w: 44
        property string tip: ""
        signal clicked()
        width: w; height: 22; radius: 3
        color: active ? th.accentBg
                      : (hov.containsMouse ? th.panelAlt : th.field)
        border.color: hov.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: 11
        }
        MouseArea {
            id: hov; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: hov.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    /* Labeled numeric field — text commit = one undoable edit. */
    component NumField: Item {
        id: nf
        property string label: ""
        property real   value: 0
        property string suffix: ""
        property int    decimals: 2
        signal committed(real v)
        width: parent ? parent.width : 0
        height: 22
        function fmt(v) { return v.toFixed(decimals) }
        function commitText() {
            var v = parseFloat(input.text)
            if (isFinite(v))
                committed(v)
        }
        onValueChanged: if (!input.activeFocus) input.text = fmt(value)
        Text {
            x: 0; anchors.verticalCenter: parent.verticalCenter
            text: nf.label; color: th.textDim; font.pixelSize: 11
            width: 52
        }
        Rectangle {
            x: 56; width: nf.width - 56 - (nf.suffix === "" ? 0 : 30)
            height: 20; anchors.verticalCenter: parent.verticalCenter
            color: th.field; radius: 3
            border.color: input.activeFocus ? th.accent : th.borderHi
            TextInput {
                id: input
                anchors.fill: parent; anchors.margins: 3
                color: th.text; font.pixelSize: 11
                text: nf.fmt(nf.value)
                selectByMouse: true
                validator: DoubleValidator { locale: "C" }
                onAccepted: { nf.commitText(); focus = false }
            }
        }
        Text {
            visible: nf.suffix !== ""
            x: nf.width - 28; anchors.verticalCenter: parent.verticalCenter
            text: nf.suffix; color: th.textFaint; font.pixelSize: 10
        }
    }

    component Lbl: Text {
        color: th.textDim; font.pixelSize: th.fontSmall
        height: 20; verticalAlignment: Text.AlignVCenter
    }

    Column {
        id: col
        width: einsp.width
        spacing: th.spHalf
        padding: th.spHalf

        property var cur: einsp.layerMap()
        property var flds: einsp.fieldsFor(cur ? cur.primitive : "")

        /* ---- header: which layer + what it needs ---- */
        Row {
            spacing: th.sp
            height: 22
            Text {
                text: col.cur ? "Layer " + (einsp.layerIndex + 1)
                              + " — " + col.cur.primitive
                            : "no layer selected"
                color: col.cur ? th.text : th.textFaint
                font.pixelSize: th.fontTitle; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: text !== ""
                text: {
                    einsp.fxStamp
                    var nb = einsp.needsBadge(col.flds.needs || "")
                    return nb.text
                }
                color: {
                    einsp.fxStamp
                    var nb = einsp.needsBadge(col.flds.needs || "")
                    return (nb.state === "off" || nb.state === "down")
                           ? th.warn : th.textDim
                }
                font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        /* ---- blend + space ---- */
        Row {
            spacing: th.spHalf; height: 22
            visible: col.cur !== null
            Lbl { text: "blend"; width: 34 }
            Repeater {
                model: [ "replace", "add", "screen" ]
                TBtn {
                    text: modelData; w: 52
                    active: col.cur && col.cur.blend === modelData
                    onClicked: {
                        var b = einsp.br()
                        if (b)
                            b.setEffectLayerBlend(einsp.layerIndex,
                                                  modelData)
                    }
                }
            }
            Item { width: 8; height: 1 }
            Lbl { text: "space"; width: 36; visible: col.flds.space === 1 }
            TBtn {
                visible: col.flds.space === 1
                text: "world"; w: 44
                active: col.cur && col.cur.space === "world"
                onClicked: {
                    var b = einsp.br()
                    if (b) b.setEffectLayerSpace(einsp.layerIndex, false)
                }
            }
            TBtn {
                visible: col.flds.space === 1
                text: "local"; w: 44
                active: col.cur && col.cur.space === "local"
                onClicked: {
                    var b = einsp.br()
                    if (b) b.setEffectLayerSpace(einsp.layerIndex, true)
                }
            }
        }

        /* ---- scalars ---- */
        NumField {
            visible: col.flds.speed === 1
            label: "speed"; value: col.cur ? col.cur.speed : 0
            suffix: "m/s"; decimals: 2
            onCommitted: function(v) { einsp.commitField("speed", v) }
        }
        NumField {
            visible: col.flds.scale === 1
            label: "scale"; value: col.cur ? col.cur.scale : 0
            decimals: 2
            onCommitted: function(v) { einsp.commitField("scale", v) }
        }
        NumField {
            visible: col.flds.phase === 1
            label: "phase"; value: col.cur ? col.cur.phase : 0
            decimals: 2
            onCommitted: function(v) { einsp.commitField("phase", v) }
        }
        NumField {
            visible: col.flds.density === 1
            label: "density"; value: col.cur ? col.cur.density : 0
            decimals: 2
            onCommitted: function(v) { einsp.commitField("density", v) }
        }
        NumField {
            visible: col.flds.seed === 1
            label: "seed"; value: col.cur ? col.cur.seed : 0
            decimals: 0
            onCommitted: function(v) { einsp.commitField("seed", v) }
        }

        /* ---- origin (mm) + viewport pick ---- */
        Column {
            visible: col.flds.origin === 1
            width: parent.width
            spacing: 2
            Row {
                spacing: th.spHalf; height: 20
                Lbl { text: "origin (mm)"; width: 100 }
                TBtn {
                    text: einsp.armedPick === "origin"
                          ? "picking…" : "pick"
                    w: 56
                    active: einsp.armedPick === "origin"
                    tip: "click/drag in the viewport — desk plane at the origin's height; one undo per pick"
                    onClicked: einsp.armPick("origin")
                }
            }
            NumField {
                label: "X"; suffix: "mm"; decimals: 1
                value: col.cur ? col.cur.origin.x * 1000 : 0
                onCommitted: function(v) {
                    einsp.commitVec("origin", 0, v) }
            }
            NumField {
                label: "Y"; suffix: "mm"; decimals: 1
                value: col.cur ? col.cur.origin.y * 1000 : 0
                onCommitted: function(v) {
                    einsp.commitVec("origin", 1, v) }
            }
            NumField {
                label: "Z"; suffix: "mm"; decimals: 1
                value: col.cur ? col.cur.origin.z * 1000 : 0
                onCommitted: function(v) {
                    einsp.commitVec("origin", 2, v) }
            }
        }

        /* ---- direction (unitless) ---- */
        Column {
            visible: col.flds.direction === 1
            width: parent.width
            spacing: 2
            Lbl { text: "direction (unitless)" }
            NumField {
                label: "X"; decimals: 2
                value: col.cur ? col.cur.direction.x : 0
                onCommitted: function(v) {
                    einsp.commitVec("direction", 0, v) }
            }
            NumField {
                label: "Y"; decimals: 2
                value: col.cur ? col.cur.direction.y : 0
                onCommitted: function(v) {
                    einsp.commitVec("direction", 1, v) }
            }
            NumField {
                label: "Z"; decimals: 2
                value: col.cur ? col.cur.direction.z : 0
                onCommitted: function(v) {
                    einsp.commitVec("direction", 2, v) }
            }
        }

        /* ---- source (reactive primitives) ---- */
        Row {
            visible: col.flds.source === 1
            spacing: th.spHalf; height: 22
            Lbl { text: "source"; width: 44 }
            Repeater {
                model: [ "", "audio", "key", "screen" ]
                TBtn {
                    text: modelData === "" ? "all" : modelData
                    w: 50
                    active: col.cur && (col.cur.source || "") === modelData
                    onClicked: {
                        var b = einsp.br()
                        if (b)
                            b.setEffectLayerSource(einsp.layerIndex,
                                                   modelData)
                    }
                }
            }
            /* disconnected hint beside the selector */
            Text {
                visible: {
                    einsp.fxStamp
                    var nb = einsp.needsBadge(col.cur ? col.cur.source : "")
                    return col.flds.source === 1 && nb.text !== ""
                           && nb.state !== "ok"
                }
                text: {
                    einsp.fxStamp
                    return einsp.needsBadge(col.cur ? col.cur.source
                                                  : "").text
                }
                color: th.warn; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        /* ---- targets (object ids / geometry tags / emitter groups) ---- */
        Column {
            visible: col.flds.targets === 1
            width: parent.width
            spacing: 2
            Lbl { text: "targets — empty means every emitter" }
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: col.cur ? col.cur.targets : []
                    delegate: Rectangle {
                        required property var modelData
                        height: 18; radius: th.radiusSm
                        width: tchipTxt.width + 24
                        color: th.field
                        border.color: th.borderHi
                        Text {
                            id: tchipTxt
                            x: 6; anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            color: th.text; font.pixelSize: th.fontSmall
                        }
                        Text {
                            x: tchipTxt.width + 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "✕"; color: th.textDim
                            font.pixelSize: th.fontSmall
                            MouseArea {
                                anchors.fill: parent
                                onClicked: einsp.removeTarget(modelData)
                            }
                        }
                    }
                }
            }
            Row {
                spacing: th.spHalf; height: 22
                ComboBox {
                    id: targetBox
                    height: 20; width: 150
                    editable: true
                    /* Known terms first — the free text still
                       accepts anything (ids validate bridge-side). */
                    model: {
                        einsp.fxStamp
                        var b = einsp.br()
                        return (b && typeof b.effectTargetIds
                                      === "function")
                               ? b.effectTargetIds() : []
                    }
                    contentItem: TextInput {
                        text: targetBox.editText
                        color: th.text; font.pixelSize: th.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 6
                        selectByMouse: true
                        onTextChanged: targetBox.editText = text
                        onAccepted: {
                            einsp.addTarget(targetBox.editText)
                            targetBox.editText = ""
                        }
                    }
                    background: Rectangle {
                        radius: th.radiusSm; color: th.field
                        border.color: targetBox.visualFocus ? th.accent
                                                            : th.borderHi
                    }
                }
                TBtn {
                    text: "add"; w: 34
                    onClicked: {
                        einsp.addTarget(targetBox.editText)
                        targetBox.editText = ""
                    }
                }
            }
        }

        /* ---- palette ---- */
        Column {
            visible: col.flds.palette === 1
            width: parent.width
            spacing: 2
            Lbl { text: "palette" }
            PaletteEditor {
                width: parent.width
                layerIndex: einsp.layerIndex
                fxStamp: einsp.fxStamp
                bridgeOverride: einsp.bridgeOverride
                hostOverride: einsp.hostOverride
            }
        }

        /* ---- path (comet) — waypoint rows + viewport append ---- */
        Column {
            visible: col.flds.path === 1
            width: parent.width
            spacing: 2
            Row {
                spacing: th.spHalf; height: 20
                Lbl { text: "path (mm) — loop"; width: 120 }
                TBtn {
                    text: einsp.armedPick === "path"
                          ? "picking…" : "pick +"
                    w: 56
                    active: einsp.armedPick === "path"
                    tip: "each viewport click appends a waypoint (drag repositions it) — one undo per click"
                    onClicked: einsp.armPick("path")
                }
            }
            Repeater {
                model: col.cur ? col.cur.path : []
                delegate: Row {
                    id: prow
                    required property var modelData
                    required property int index
                    spacing: 3; height: 20
                    Lbl { text: "#" + prow.index; width: 24 }
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            required property int index
                            width: 46; height: 18; radius: 3
                            color: th.field
                            border.color: pin.activeFocus ? th.accent
                                                          : th.borderHi
                            TextInput {
                                id: pin
                                anchors.fill: parent
                                anchors.margins: 2
                                color: th.text; font.pixelSize: 10
                                selectByMouse: true
                                validator: DoubleValidator { locale: "C" }
                                text: (index === 0 ? prow.modelData.x
                                      : index === 1 ? prow.modelData.y
                                      : prow.modelData.z) * 1000
                                onAccepted: {
                                    einsp.commitPath(prow.index,
                                                     index,
                                                     parseFloat(text))
                                    focus = false
                                }
                            }
                        }
                    }
                    TBtn {
                        text: "✕"; w: 18
                        onClicked: {
                            var b = einsp.br()
                            if (b)
                                b.removeEffectLayerPathPoint(
                                    einsp.layerIndex, prow.index)
                        }
                    }
                }
            }
        }
    }
}
