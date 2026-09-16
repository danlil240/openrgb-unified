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
        function onInputsChanged()       { shelf.syncAll() }
        function onBrightnessChanged()   { brightSl.sync() }
    }

    implicitHeight: wrap.height

    Theme { id: th }

    /* Dark slider — groove + handle, explicit focus ring. */
    component CSlider: Slider {
        property int w: 110
        implicitWidth: w; implicitHeight: 22
        function sync() {}
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
    }

    component SCheck: Rectangle {
        property string text: ""
        property bool value: false
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
                    height: 34; width: cardLabel.width + 20
                    radius: th.radiusSm
                    property bool on: {
                        shelf.fxStamp
                        var b = shelf.br()
                        return b ? (b.playing
                                    && b.activePreset === modelData.id)
                                 : false
                    }
                    color: on ? th.accentBg
                         : (cardHov.containsMouse ? th.panelAlt : th.field)
                    border.color: on ? th.accent : th.borderHi
                    function activate() {
                        if (shelf.hasBr())
                            shelf.br().playPreset(card.modelData.id)
                    }
                    Text {
                        id: cardLabel
                        anchors.centerIn: parent
                        text: card.modelData.name
                        color: th.text; font.pixelSize: th.fontBody
                    }
                    MouseArea {
                        id: cardHov; anchors.fill: parent; hoverEnabled: true
                        onClicked: card.activate()
                    }
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
                enabled: {
                    shelf.fxStamp
                    return shelf.hasBr() && shelf.br().activePreset !== ""
                }
                onClicked: if (shelf.hasBr()) shelf.br().remix()
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
                    if (shelf.hasBr())
                        currentIndex = shelf.br().screenIndex()
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
