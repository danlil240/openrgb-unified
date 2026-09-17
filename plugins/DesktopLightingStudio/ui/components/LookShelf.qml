/*---------------------------------------------------------*\
||| LookShelf.qml                                           ||
|||                                                         ||
|||   Bottom "looks" shelf (spec §3): effect preset cards, ||
|||   Play/Pause + Stop + Remix, speed/intensity and       ||
|||   brightness sliders — the old C++ fx bar plus the     ||
|||   scene-bar brightness, re-homed in QML. A collapsible ||
|||   Inputs row keeps the Stage-3 reactive sources        ||
|||   (audio/keys/screen + sens/decay/screen picker).      ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: shelf
    objectName: "lookShelf"
    color: th.panel

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

    /* Task 5.2 — the workspace hosts the effect editor; the
       shelf only asks. */
    signal editRequested()

    /* Required-input badge text for a look's `needs` —
       icon glyph + words (never color alone). state:
       "" = unknown, "ok" = live, "off" = toggled off,
       "down" = provider not running. */
    function needsBadge(needs) {
        if (!needs || needs === "")
            return { text: "", state: "" }
        var b = br()
        if (!b || typeof b.inputSourceState !== "function")
            return { text: needs, state: "" }
        var st = b.inputSourceState(needs)
        if (!st || st.name === undefined)
            return { text: needs, state: "" }
        if (!st.enabled)
            return { text: needs + " — off", state: "off" }
        if (!st.ready)
            return { text: needs + " — " + (st.status || "disconnected"),
                     state: "down" }
        return { text: needs, state: "ok" }
    }

    property bool inputsOpen: false
    /* Bump on bridge signals so the stubs/tests re-read too. */
    property int fxStamp: 0
    function syncAll() {
        speedSl.sync(); intenSl.sync(); brightSl.sync()
        sensSl.sync();  decaySl.sync()
        audioChk.sync(); keyChk.sync(); screenChk.sync(); screenBox.sync()
    }

    Connections {
        target: shelf.brQObj()
        function onPlayingChanged()      { shelf.fxStamp++ }
        function onPresetChanged()       { shelf.fxStamp++ }
        function onEffectParamsChanged() {
            shelf.fxStamp++
            speedSl.sync(); intenSl.sync()
        }
        function onInputsChanged()       { shelf.syncAll(); shelf.fxStamp++ }
        function onBrightnessChanged()   { brightSl.sync() }
    }

    implicitHeight: wrap.height

    Theme { id: th }

    /* Dark slider — groove + handle, explicit focus ring. */
    component CSlider: Slider {
        property int w: 110
        property string tip: ""
        implicitWidth: w; implicitHeight: 22
        function sync() {}
        ToolTip.visible: hovered && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
        background: Rectangle {
            x: parent.leftPadding
            y: parent.topPadding + parent.availableHeight / 2 - 2
            width: parent.availableWidth; height: 4
            radius: 2; color: th.field
            Rectangle {
                width: parent.parent.visualPosition * parent.width
                height: parent.height; radius: 2; color: th.accentBg
            }
        }
        handle: Rectangle {
            x: parent.leftPadding + parent.visualPosition
               * (parent.availableWidth - width)
            y: parent.topPadding + parent.availableHeight / 2 - height / 2
            width: 14; height: 14; radius: 7
            color: parent.pressed ? th.accent
                : (parent.visualFocus ? th.accent : th.textDim)
            border.color: th.border
        }
    }

    component SLabel: Text {
        color: th.textDim; font.pixelSize: th.fontSmall
        verticalAlignment: Text.AlignVCenter
    }

    component PctLabel: Text {
        property int pct: 0
        text: pct + "%"
        color: th.textDim; font.pixelSize: th.fontSmall
        verticalAlignment: Text.AlignVCenter
        width: 38
    }

    component SButton: Rectangle {
        property string text: ""
        property bool active: false
        property int w: 56
        property string tip: ""
        signal clicked()
        width: w; height: 26; radius: th.radiusSm
        color: active ? th.accentBg
             : (btnMa.containsMouse ? th.panelAlt : th.field)
        border.color: btnMa.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
        }
        MouseArea {
            id: btnMa; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
        ToolTip.visible: btnMa.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    component SCheck: Rectangle {
        property string text: ""
        property bool value: false
        property string tip: ""
        signal toggled(bool on)
        function sync() {}
        width: row.width + 8; height: 26; radius: th.radiusSm
        color: chkMa.containsMouse ? th.panelAlt : "transparent"
        border.color: chkMa.activeFocus ? th.accent : "transparent"
        Row {
            id: row; x: 4; spacing: 6
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                width: 14; height: 14; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: value ? th.accentBg : th.field
                border.color: value ? th.accent : th.borderHi
                Text {
                    anchors.centerIn: parent
                    visible: value; text: "✓"
                    color: th.text; font.pixelSize: th.fontSmall
                }
            }
            Text {
                text: parent.parent.text
                color: th.text; font.pixelSize: th.fontBody
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea {
            id: chkMa; anchors.fill: parent; hoverEnabled: true
            activeFocusOnTab: true
            onClicked: { value = !value; toggled(value) }
            Keys.onSpacePressed:  { value = !value; toggled(value) }
            Keys.onReturnPressed: { value = !value; toggled(value) }
        }
        ToolTip.visible: chkMa.containsMouse && tip !== ""
        ToolTip.text: tip
        ToolTip.delay: 500
    }

    Column {
        id: wrap
        width: shelf.width
        spacing: 0

        /*------------ preset cards + playback ------------*/
        Row {
            id: shelfRow
            width: parent.width
            height: 48
            spacing: th.sp
            leftPadding: th.sp; rightPadding: th.sp

            Text {
                text: "Looks"
                color: th.text; font.pixelSize: th.fontTitle; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }

            ListView {
                id: presetRow
                objectName: "presetRow"
                orientation: ListView.Horizontal
                height: 34
                width: Math.min(contentWidth, shelf.width * 0.34)
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                spacing: th.spHalf
                model: {
                    shelf.fxStamp
                    return shelf.hasBr() ? shelf.br().presetList : []
                }
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    height: 34; width: cardCol.width + 20
                    radius: th.radiusSm
                    property bool on: {
                        shelf.fxStamp
                        var b = shelf.br()
                        return b ? (b.playing
                                    && b.activePreset === modelData.id)
                                 : false
                    }
                    /* needs badge — always icon+word; the
                       disconnected suffix shows on the ACTIVE or
                       hovered card when the required source is
                       toggled off or its provider is down. */
                    property var nb: {
                        shelf.fxStamp
                        cardHov.containsMouse
                        return shelf.needsBadge(card.modelData.needs || "")
                    }
                    color: on ? th.accentBg
                         : (cardHov.containsMouse ? th.panelAlt : th.field)
                    border.color: on ? th.accent : th.borderHi
                    function activate() {
                        if (shelf.hasBr())
                            shelf.br().playPreset(card.modelData.id)
                    }
                    Column {
                        id: cardCol
                        anchors.centerIn: parent
                        Text {
                            id: cardLabel
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: card.modelData.name
                                   + (card.modelData.fromFile ? " •" : "")
                            color: th.text; font.pixelSize: th.fontBody
                        }
                        Text {
                            visible: text !== ""
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: {
                                var nb = card.nb
                                if (nb.state === "" || nb.state === "ok")
                                    return nb.text
                                if (card.on || cardHov.containsMouse)
                                    return nb.text      /* off/down msg */
                                return card.modelData.needs || ""
                            }
                            color: (card.nb.state === "off"
                                    || card.nb.state === "down")
                                   ? th.warn : th.textFaint
                            font.pixelSize: th.fontSmall
                        }
                    }
                    MouseArea {
                        id: cardHov; anchors.fill: parent; hoverEnabled: true
                        onClicked: card.activate()
                    }
                    /* The old cards carried the preset's description
                       as a tooltip — restored (was dropped in 3.1). */
                    ToolTip.visible: cardHov.containsMouse
                                     && (card.modelData.description || "") !== ""
                    ToolTip.text: {
                        var d = card.modelData.description || ""
                        var nb = card.nb
                        var n = (nb.state === "off" || nb.state === "down")
                              ? nb.text : ""
                        return n === "" ? d
                               : (d === "" ? n : d + " — " + n)
                    }
                    ToolTip.delay: 500
                }
            }

            SButton {
                text: {
                    shelf.fxStamp
                    return (shelf.hasBr() && shelf.br().playing)
                           ? "Pause" : "Play"
                }
                w: 56
                onClicked: {
                    var b = shelf.br()
                    if (!b)
                        return
                    if (b.playing) {
                        b.setPlaying(false)
                    } else if (b.activePreset === "") {
                        var p = b.presetList
                        if (p.length)
                            b.playPreset(p[0].id)
                    } else {
                        b.setPlaying(true)
                    }
                    shelf.fxStamp++
                }
            }
            SButton {
                text: "Stop"; w: 48
                onClicked: if (shelf.hasBr()) shelf.br().stopEffect()
            }
            SButton {
                text: "Remix"; w: 56
                tip: "Re-roll this preset's random choices (seeded, reproducible)"
                enabled: {
                    shelf.fxStamp
                    return shelf.hasBr() && shelf.br().activePreset !== ""
                }
                onClicked: if (shelf.hasBr()) shelf.br().remix()
            }
            SButton {
                text: "Edit"; w: 46
                tip: "Edit the look's layer stack — order, blending, palettes, targets"
                enabled: {
                    shelf.fxStamp
                    var b = shelf.br()
                    if (!b)
                        return false
                    /* Something to edit: a named look (materializes
                       on first edit) or an existing inline stack. */
                    if (b.activePreset !== "")
                        return true
                    return (typeof b.effectLayerCount === "function")
                           && b.effectLayerCount() > 0
                }
                onClicked: shelf.editRequested()
            }

            Item { width: 4; height: 1 }

            SLabel { text: "Speed"; anchors.verticalCenter: parent.verticalCenter }
            CSlider {
                id: speedSl
                from: 10; to: 400
                anchors.verticalCenter: parent.verticalCenter
                onMoved: {
                    speedPct.pct = Math.round(value)
                    if (shelf.hasBr())
                        shelf.br().setEffectSpeedPct(Math.round(value))
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().effectSpeedPct : 100
                    speedPct.pct = Math.round(value)
                }
            }
            PctLabel { id: speedPct; pct: 100
                       anchors.verticalCenter: parent.verticalCenter }

            SLabel { text: "Intensity"; anchors.verticalCenter: parent.verticalCenter }
            CSlider {
                id: intenSl
                from: 0; to: 100; w: 90
                anchors.verticalCenter: parent.verticalCenter
                onMoved: {
                    intenPct.pct = Math.round(value)
                    if (shelf.hasBr())
                        shelf.br().setEffectIntensityPct(Math.round(value))
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().effectIntensityPct : 100
                    intenPct.pct = Math.round(value)
                }
            }
            PctLabel { id: intenPct; pct: 100
                       anchors.verticalCenter: parent.verticalCenter }

            Item { width: 4; height: 1 }

            SLabel { text: "Brightness"; anchors.verticalCenter: parent.verticalCenter }
            CSlider {
                id: brightSl
                from: 0; to: 100; w: 90
                anchors.verticalCenter: parent.verticalCenter
                onMoved: {
                    brightPct.pct = Math.round(value)
                    if (shelf.hasBr()) {
                        shelf.br().setBrightnessPct(Math.round(value))
                        shelf.fxStamp++
                    }
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().brightnessPct : 100
                    brightPct.pct = Math.round(value)
                }
            }
            PctLabel { id: brightPct; pct: 100
                       anchors.verticalCenter: parent.verticalCenter }

            Item { width: 4; height: 1 }

            SButton {
                text: shelf.inputsOpen ? "Inputs ▾" : "Inputs ▸"
                w: 76
                onClicked: shelf.inputsOpen = !shelf.inputsOpen
            }
        }

        /*------------ reactive inputs (Stage 3) ------------*/
        Row {
            id: inputsRow
            visible: shelf.inputsOpen
            width: parent.width
            height: visible ? 40 : 0
            spacing: th.sp
            leftPadding: th.sp; rightPadding: th.sp

            SCheck {
                id: audioChk
                text: "Audio"
                tip: "System audio loopback — onsets drive shockwave rings"
                anchors.verticalCenter: parent.verticalCenter
                onToggled: function(on) {
                    if (shelf.hasBr()) shelf.br().setAudioInput(on)
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().audioInput : false
                }
            }
            SLabel { text: "Sens"; anchors.verticalCenter: parent.verticalCenter }
            CSlider {
                id: sensSl
                from: 25; to: 200; w: 80
                anchors.verticalCenter: parent.verticalCenter
                onMoved: {
                    sensPct.pct = Math.round(value)
                    if (shelf.hasBr())
                        shelf.br().setAudioSensitivityPct(Math.round(value))
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().audioSensitivityPct() : 100
                    sensPct.pct = Math.round(value)
                }
            }
            PctLabel { id: sensPct; pct: 100
                       anchors.verticalCenter: parent.verticalCenter }

            SCheck {
                id: keyChk
                text: "Keys"
                tip: "Low-level keyboard hook — key presses spawn ripples at the mapped key"
                anchors.verticalCenter: parent.verticalCenter
                onToggled: function(on) {
                    if (shelf.hasBr()) shelf.br().setKeyInput(on)
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().keyInput : false
                }
            }
            SCheck {
                id: screenChk
                text: "Screen"
                tip: "Sample the display — ambient colors wash over the setup"
                anchors.verticalCenter: parent.verticalCenter
                onToggled: function(on) {
                    if (shelf.hasBr()) shelf.br().setScreenInput(on)
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().screenInput : false
                }
            }
            ComboBox {
                id: screenBox
                height: 26; width: 170
                anchors.verticalCenter: parent.verticalCenter
                /* Display names need ScreenSampler (C++); the host
                   seam keeps the QML free of that dependency. */
                model: shelf.host() ? shelf.host().uiScreenNames() : []
                onActivated: function(i) {
                    if (shelf.hasBr()) shelf.br().setScreenIndex(i)
                }
                function sync() {
                    if (!shelf.hasBr())
                        return
                    /* Saved index can exceed the display count after
                       a monitor unplug — the old combo guarded the
                       same assignment. */
                    var i = shelf.br().screenIndex()
                    if (i >= 0 && i < count)
                        currentIndex = i
                }
                contentItem: Text {
                    text: screenBox.displayText
                    color: th.text; font.pixelSize: th.fontBody
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8; elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: th.radiusSm; color: th.field
                    border.color: screenBox.visualFocus ? th.accent
                                                        : th.borderHi
                }
            }

            Item { width: 4; height: 1 }

            SLabel { text: "Decay"; anchors.verticalCenter: parent.verticalCenter }
            CSlider {
                id: decaySl
                from: 50; to: 300; w: 80
                tip: "Ripple ring lifetime — higher decays faster"
                anchors.verticalCenter: parent.verticalCenter
                onMoved: {
                    decayPct.pct = Math.round(value)
                    if (shelf.hasBr()) shelf.br().setRippleDecayPct(Math.round(value))
                }
                function sync() {
                    value = shelf.hasBr() ? shelf.br().rippleDecayPct() : 100
                    decayPct.pct = Math.round(value)
                }
            }
            PctLabel { id: decayPct; pct: 100
                       anchors.verticalCenter: parent.verticalCenter }
        }
    }

    Component.onCompleted: syncAll()
}
