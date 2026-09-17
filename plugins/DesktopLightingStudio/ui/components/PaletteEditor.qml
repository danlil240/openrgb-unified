/*---------------------------------------------------------*\
|||| PaletteEditor.qml                                       ||
||||                                                         ||
||||   Palette-stop strip for one effect layer (task 5.2):  ||
||||   a proportional swatch band on top, one row per stop  ||
||||   — color swatch (click opens the host's color        ||
||||   dialog through uiPickColorFor), hex field, position ||
||||   slider, remove button — plus an "add stop" action   ||
||||   that seeds the widest gap's midpoint.               ||
||||                                                         ||
||||   Undo contract: recolor/remove/add are one undoable  ||
||||   bridge edit each; the position slider wraps its     ||
||||   scrub in begin/commitEffectGesture so a drag lands  ||
||||   a single command. Reads the layer through           ||
||||   bridge.effectLayer(i) — plain QVariantMap, so the   ||
||||   stub bridge in the QML suite drives it unchanged.   ||
||||                                                         ||
||||   SPDX-License-Identifier: GPL-2.0-or-later             ||
|\*---------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Column {
    id: pal
    objectName: "paletteEditor"
    spacing: th.spHalf

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

    /* The layer this palette belongs to (-1 = none). */
    property int layerIndex: -1
    /* Re-eval stamp — the host panel bumps it on
       effectLayersChanged; tests bump it directly. Each bump
       refreshes stopCache — EXCEPT mid-scrub: rebuilding the stop
       rows while a position slider is pressed destroys the
       delegate mid-gesture and leaks the undo gesture. */
    property int fxStamp: 0
    onFxStampChanged: refreshStops()
    onLayerIndexChanged: refreshStops()
    onBridgeOverrideChanged: refreshStops()
    function poke() { fxStamp++ }

    /* The repeaters' model — a CACHED snapshot of stops(). */
    property var stopCache: []
    property bool scrubbing: false
    property bool pendingStops: false
    function refreshStops() {
        if (scrubbing) {
            pendingStops = true
            return
        }
        stopCache = stops()
    }
    Component.onCompleted: refreshStops()

    /* [{pos: 0..1, color: "#rrggbb"}] — bridge.effectLayer(i)
       carries the stops list under `palette`. Always a FRESH
       read (callers that need the live list use this; the
       repeaters bind the cache). */
    function stops() {
        var b = br()
        if (!b || layerIndex < 0
            || typeof b.effectLayer !== "function")
            return []
        var l = b.effectLayer(layerIndex)
        return (l && l.palette) ? l.palette : []
    }

    /* Widest gap midpoint — the seed position for a new stop.
       First stop defaults to 0.5. */
    function addPos() {
        var s = stops()
        if (s.length === 0)
            return 0.5
        var best = 0.0, gap = s[0].pos          /* gap before first */
        var cand = s[0].pos / 2
        for (var i = 0; i + 1 < s.length; i++) {
            var g = s[i + 1].pos - s[i].pos
            if (g > gap) { gap = g; best = s[i].pos + g / 2; cand = best }
        }
        if (1.0 - s[s.length - 1].pos > gap)    /* gap after last */
            cand = (1.0 + s[s.length - 1].pos) / 2
        return cand
    }

    /* Host color dialog → "#rrggbb" or "" on cancel. Without a
       host (tests, standalone) returns "" and the hex field is
       the fallback path. */
    function pickColor(stopIdx) {
        var s = stops()
        var h = host()
        if (!h || typeof h.uiPickColorFor !== "function")
            return
        var cur = (s[stopIdx] && s[stopIdx].color) || "#ffffff"
        var c = h.uiPickColorFor(cur)
        if (c && c !== "" && hasBr())
            br().setEffectLayerStopColor(layerIndex, stopIdx, c)
    }

    /* ---- stop actions — delegates call these; tests drive them
       directly (each = one undoable bridge edit). ---- */
    function removeStop(i) {
        var b = br()
        if (b) b.removeEffectLayerStop(layerIndex, i)
    }
    function setStopColor(i, c) {
        var b = br()
        if (b && /^#[0-9a-fA-F]{6}$/.test(c))
            b.setEffectLayerStopColor(layerIndex, i, c)
    }
    /* Position scrub = one effect gesture = one undo record.
       scrubbing freezes the stop rows for the drag; dragStop
       tracks the dragged stop's CURRENT index — the bridge op
       re-sorts the palette on every move, so a stop crossing its
       neighbor changes index mid-gesture and a stale row index
       would move the WRONG stop. */
    property int dragStop: -1
    function posScrubBegin(i) {
        var b = br()
        dragStop = (typeof i === "number") ? i : -1
        scrubbing = true
        if (b) b.beginEffectGesture()
    }
    function posScrubMove(i, v) {
        var b = br()
        if (!b)
            return
        var idx = (dragStop >= 0) ? dragStop : i
        var ni = b.moveEffectLayerStop(layerIndex, idx, v)
        /* The real bridge returns the post-sort index; stubs that
           return undefined keep index-following (pre-review
           behavior). */
        if (typeof ni === "number" && ni >= 0)
            dragStop = ni
    }
    function posScrubEnd() {
        var b = br()
        dragStop = -1
        scrubbing = false
        if (b) b.commitEffectGesture("move palette stop")
        if (pendingStops) {
            pendingStops = false
            refreshStops()
        }
    }
    function doAddStop() {
        var b = br()
        if (!b)
            return
        var s = stops()
        var c = s.length ? s[s.length - 1].color : "#ffffff"
        b.addEffectLayerStop(layerIndex, addPos(), c)
    }

    Theme { id: th }

    /* ---------- swatch band: stop colors at their positions --- */
    Rectangle {
        id: strip
        width: pal.width; height: 18
        radius: th.radiusSm
        color: th.field
        border.color: th.border
        clip: true
        Repeater {
            model: pal.stopCache
            delegate: Rectangle {
                required property var modelData
                required property int index
                /* Span from this stop's pos to the next (or 1). */
                x: strip.width * modelData.pos
                width: {
                    var s = pal.stopCache
                    var next = (index + 1 < s.length) ? s[index + 1].pos
                                                      : 1.0
                    return Math.max(2, strip.width * (next - modelData.pos))
                }
                height: strip.height
                color: modelData.color
            }
        }
        Text {
            anchors.centerIn: parent
            visible: pal.stopCache.length === 0
            text: "empty palette"
            color: th.textFaint; font.pixelSize: th.fontSmall
        }
    }

    /* ---------- per-stop rows ---------- */
    Repeater {
        id: stopRows
        model: pal.stopCache
        delegate: Row {
            id: srow
            required property var modelData
            required property int index
            spacing: th.spHalf
            height: 22

            /* Safety net: a mid-press rebuild must not leak the
               gesture — end it so the drag still lands its one
               undo record. */
            Component.onDestruction: {
                if (posSl.pressed)
                    pal.posScrubEnd()
            }

            /* swatch — click opens the host color dialog */
            Rectangle {
                width: 30; height: 18; radius: th.radiusSm
                anchors.verticalCenter: parent.verticalCenter
                color: srow.modelData.color
                border.color: swMa.containsMouse ? th.accent
                                                 : th.borderHi
                MouseArea {
                    id: swMa; anchors.fill: parent; hoverEnabled: true
                    onClicked: pal.pickColor(srow.index)
                }
                ToolTip.visible: swMa.containsMouse
                ToolTip.text: "recolor stop"
                ToolTip.delay: 500
            }

            /* hex field — the no-host path (and fine edits) */
            Rectangle {
                width: 62; height: 20; radius: th.radiusSm
                anchors.verticalCenter: parent.verticalCenter
                color: th.field
                border.color: hexIn.activeFocus ? th.accent
                                                : th.borderHi
                TextInput {
                    id: hexIn
                    anchors.fill: parent; anchors.margins: 3
                    color: th.text; font.pixelSize: th.fontSmall
                    text: srow.modelData.color
                    selectByMouse: true
                    onAccepted: {
                        pal.setStopColor(srow.index, text)
                        focus = false
                    }
                }
            }

            /* position — gesture-wrapped slider scrub */
            Slider {
                id: posSl
                width: 90; height: 20
                anchors.verticalCenter: parent.verticalCenter
                from: 0; to: 1
                value: srow.modelData.pos
                onPressedChanged: {
                    if (pressed)
                        pal.posScrubBegin(srow.index)
                    else
                        pal.posScrubEnd()
                }
                onMoved: pal.posScrubMove(srow.index, value)
                background: Rectangle {
                    x: posSl.leftPadding
                    y: posSl.topPadding + posSl.availableHeight / 2 - 1
                    width: posSl.availableWidth; height: 3
                    radius: 1.5; color: th.field
                }
                handle: Rectangle {
                    x: posSl.leftPadding + posSl.visualPosition
                       * (posSl.availableWidth - width)
                    y: posSl.topPadding + posSl.availableHeight / 2
                       - height / 2
                    width: 10; height: 10; radius: 5
                    color: posSl.pressed ? th.accent : th.textDim
                    border.color: th.border
                }
            }

            Text {
                text: (Math.round(srow.modelData.pos * 100) / 100)
                          .toFixed(2)
                color: th.textDim; font.pixelSize: th.fontSmall
                width: 30
                anchors.verticalCenter: parent.verticalCenter
            }

            /* remove — one undoable edit */
            Rectangle {
                width: 18; height: 18; radius: th.radiusSm
                anchors.verticalCenter: parent.verticalCenter
                color: delMa.containsMouse ? th.panelAlt : th.field
                border.color: th.borderHi
                Text {
                    anchors.centerIn: parent
                    text: "✕"; color: th.text
                    font.pixelSize: th.fontSmall
                }
                MouseArea {
                    id: delMa; anchors.fill: parent; hoverEnabled: true
                    onClicked: pal.removeStop(srow.index)
                }
            }
        }
    }

    /* ---------- add ---------- */
    Rectangle {
        width: addTxt.width + 14; height: 20; radius: th.radiusSm
        color: addMa.containsMouse ? th.panelAlt : th.field
        border.color: addMa.activeFocus ? th.accent : th.borderHi
        function doAdd() { pal.doAddStop() }
        Text {
            id: addTxt; anchors.centerIn: parent
            /* fxStamp read keeps the seed-position label fresh —
               stops() itself is a pure read now (the repeaters
               bind stopCache). */
            text: { pal.fxStamp
                    return "+ stop @ " + pal.addPos().toFixed(2) }
            color: th.text; font.pixelSize: th.fontSmall
        }
        MouseArea {
            id: addMa; anchors.fill: parent; hoverEnabled: true
            activeFocusOnTab: true
            onClicked: parent.doAdd()
            Keys.onSpacePressed:  parent.doAdd()
            Keys.onReturnPressed: parent.doAdd()
        }
    }
}
