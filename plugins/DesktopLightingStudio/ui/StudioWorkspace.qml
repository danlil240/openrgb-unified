/*---------------------------------------------------------*\
||| StudioWorkspace.qml                                     ||
|||                                                         ||
|||   Unified workspace shell (Studio Next, task 3.1).     ||
|||   New ROOT loaded by StudioTab's QQuickWidget —        ||
|||   embeds StudioScene as the viewport and wraps it in   ||
|||   the spec-§3 chrome:                                  ||
|||                                                         ||
|||     header   File · save state · Undo/Redo · View      ||
|||              presets · Preview|Live · panel toggles    ||
|||     left     DeviceTree (searchable hierarchy)         ||
|||     center   StudioScene viewport (unchanged)          ||
|||     right    Inspector (docked; overlay drawer when    ||
|||              narrow)                                   ||
|||     bottom   LookShelf + collapsible Diagnostics       ||
|||              drawer                                    ||
|||                                                         ||
|||   C++ seams (context properties set by StudioTab):     ||
|||     bridge      SceneBridge — scene/effects/inputs     ||
|||     studioHost  StudioTab   — file dialogs, probes     ||
|||   Both are optional (`typeof`/`br()` guards) so the    ||
|||   file still instantiates bare under qmltestrunner.    ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtQuick.Controls.Basic
import "components" as C

Rectangle {
    id: ws
    objectName: "studioWorkspace"
    color: th.bg

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
    function hasBr()   { return br() !== null }
    function hasHost() { return host() !== null }
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

    /* Layout state */
    property int  hdrH:      40
    property int  treeW:     236
    property int  inspW:     312
    property int  diagH:     190
    property bool narrow:    ws.width < 1040
    property bool inspOpen:  false        /* drawer when narrow */
    property bool diagOpen:  false
    property string diagLog: ""

    /* Re-eval stamps for the stub/test path (no NOTIFY). */
    property int stamp: 0
    function poke() { stamp++ }

    /* Test accessors — objectName-based findChild doesn't reach
       QML ids; expose the panels for the qmltestrunner suite. */
    readonly property var diagDrawer: diag
    readonly property var treePanel:  devTree
    readonly property var shelfPanel: shelf
    readonly property var inspPanel:  insp
    readonly property var sceneView:  scene

    function logLine(s) {
        diagLog += (diagLog.length ? "\n" : "") + s
        /* Cap the log — the drawer is a tail view, not a file. */
        if (diagLog.length > 20000)
            diagLog = diagLog.substring(diagLog.length - 16000)
    }

    Connections {
        target: ws.brQObj()
        function onSelectionChanged() { ws.stamp++ }
        function onUndoChanged()      { ws.stamp++ }
        function onDirtyChanged()     { ws.stamp++ }
        function onLiveChanged()      { ws.stamp++; liveChk.sync() }
        function onCaseGhostChanged() { ws.stamp++; ghostChk.sync() }
        function onStatusChanged()    { ws.stamp++ }
        /* statusMessage is NOT logged here — StudioTab relays it
           through AppendResult -> diagnosticsLine (one log path). */
    }
    Connections {
        target: ws.hostQObj()
        function onDiagnosticsLine(t)              { ws.logLine(t) }
        function onDiagnosticsControllersChanged() { ctrlBox.refresh() }
    }

    C.Theme { id: th }

    /* Local copies of the shared chrome controls (the component
       files keep their own; these are the header/drawer set). */
    component HBtn: Rectangle {
        property string text: ""
        property bool   active: false
        property int    w: 52
        signal clicked()
        width: w; height: 24; radius: th.radiusSm
        color: active ? th.accentBg
             : (hma.containsMouse ? th.panelAlt : th.field)
        border.color: hma.activeFocus ? th.accent
                    : (active ? th.accent : th.borderHi)
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
        }
        MouseArea {
            id: hma; anchors.fill: parent; hoverEnabled: true
            enabled: parent.enabled
            activeFocusOnTab: true
            onClicked: parent.clicked()
            Keys.onSpacePressed:  parent.clicked()
            Keys.onReturnPressed: parent.clicked()
        }
    }

    component HCheck: Rectangle {
        property string text: ""
        property bool   value: false
        signal toggled(bool on)
        width: rowc.width + 10; height: 24; radius: th.radiusSm
        color: cma.containsMouse ? th.panelAlt : "transparent"
        border.color: cma.activeFocus ? th.accent : "transparent"
        Row {
            id: rowc; x: 5; spacing: 6
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                width: 13; height: 13; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: value ? th.accentBg : th.field
                border.color: value ? th.accent : th.borderHi
            }
            Text {
                text: parent.parent.text
                color: th.text; font.pixelSize: th.fontBody
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        MouseArea {
            id: cma; anchors.fill: parent; hoverEnabled: true
            activeFocusOnTab: true
            onClicked: { value = !value; toggled(value) }
            Keys.onSpacePressed:  { value = !value; toggled(value) }
            Keys.onReturnPressed: { value = !value; toggled(value) }
        }
    }

    /* Drag handle for the resizable sidebars — reports the raw
       pixel delta; each owner clamps + applies its own width. */
    component Splitter: Rectangle {
        signal pressed()
        signal moved(int dx)
        width: 5
        color: sma.containsMouse || sma.pressed ? th.borderHi
                                                : "transparent"
        MouseArea {
            id: sma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real startX: 0
            onPressed: function(m) {
                startX = mapToItem(ws, m.x, m.y).x
                parent.pressed()
            }
            onPositionChanged: function(m) {
                if (pressed)
                    parent.moved(Math.round(
                        mapToItem(ws, m.x, m.y).x - startX))
            }
        }
    }

    component SMenuItem: MenuItem {
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? th.text : th.textFaint
            font.pixelSize: th.fontBody
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        background: Rectangle {
            color: parent.highlighted ? th.panelAlt : th.panel
            radius: th.radiusSm
        }
    }

    /*======================= HEADER =======================*/
    Rectangle {
        id: header
        x: 0; y: 0; width: ws.width; height: ws.hdrH
        color: th.panel
        /* Below ~900 px the left/right rows can collide — clip any
           spill rather than overlapping paint. */
        clip: true
        Rectangle { anchors.bottom: parent.bottom; width: parent.width
                    height: 1; color: th.border }

        Row {
            x: th.sp; spacing: th.spHalf
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height

            /* File — dialogs/confirmations stay C++ (studioHost). */
            HBtn {
                id: fileBtn
                text: "File"; w: 46
                enabled: ws.hasHost()
                onClicked: fileMenu.popup()
                Menu {
                    id: fileMenu
                    y: fileBtn.height + 4
                    background: Rectangle {
                        color: th.panel; radius: th.radiusSm
                        border.color: th.border
                    }
                    SMenuItem { text: "Save"
                        onTriggered: ws.host().uiSave() }
                    SMenuItem { text: "Save Copy As…"
                        onTriggered: ws.host().uiSaveCopyAs() }
                    SMenuItem { text: "Reload studio.json"
                        onTriggered: ws.host().uiReload() }
                    MenuSeparator {
                        contentItem: Rectangle {
                            implicitWidth: 160; implicitHeight: 1
                            color: th.border
                        }
                    }
                    SMenuItem { text: "Reset to Default Desk"
                        onTriggered: ws.host().uiReset() }
                    MenuSeparator {
                        contentItem: Rectangle {
                            implicitWidth: 160; implicitHeight: 1
                            color: th.border
                        }
                    }
                    SMenuItem { text: "Open Config Folder"
                        onTriggered: ws.host().uiOpenWorkspaceFolder() }
                    SMenuItem { text: "Restore Backup"
                        onTriggered: ws.host().uiRestoreBackup() }
                }
            }

            /* Project / saved status */
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    ws.stamp
                    var b = ws.br()
                    var doc = (b && b.documentPath) ? b.documentPath : "studio.json"
                    var base = doc.substring(doc.lastIndexOf("/") + 1)
                    return (b && b.dirty) ? base + "  ● unsaved" : base
                }
                color: {
                    ws.stamp
                    return (ws.hasBr() && ws.br().dirty) ? th.warn : th.textDim
                }
                font.pixelSize: th.fontBody
            }

            Item { width: th.spHalf; height: 1 }

            HBtn {
                text: "Save"; w: 46
                enabled: ws.hasHost()
                onClicked: ws.host().uiSave()
            }
            HBtn {
                text: "Undo"; w: 48
                enabled: {
                    ws.stamp
                    return ws.hasBr() && ws.br().canUndo
                }
                onClicked: scene.undoOrRedo(false)
            }
            HBtn {
                text: "Redo"; w: 48
                enabled: {
                    ws.stamp
                    return ws.hasBr() && ws.br().canRedo
                }
                onClicked: scene.undoOrRedo(true)
            }

            Item { width: th.sp; height: 1 }

            /* View presets (same row the inspector exposes — spec
               header sketch keeps them in the top bar). */
            Text {
                text: "View"; color: th.textDim; font.pixelSize: th.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
            Repeater {
                model: ["desk", "top", "front", "case", "free"]
                HBtn {
                    required property string modelData
                    text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                    w: 46
                    active: scene.editorCam
                            && scene.editorCam.viewName === modelData
                    onClicked: if (scene.editorCam)
                        scene.editorCam.applyView(modelData)
                }
            }

            Item { width: th.sp; height: 1 }

            /* Selection readout (the old scene-bar label — prefers
               the object's display label over the raw id). */
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 180)
                elide: Text.ElideRight
                /* Below ~900 px the header clusters collide — this
                   readout is the first thing to give way. */
                visible: ws.width > 900
                text: {
                    ws.stamp
                    var b = ws.br()
                    if (!b)
                        return ""
                    if ((b.selectedId || "") === "")
                        return "(nothing selected)"
                    var info = b.objectInfo ? b.objectInfo(b.selectedId)
                                            : {}
                    return "selected: " + (info.label || b.selectedId)
                }
                color: th.textDim; font.pixelSize: th.fontSmall
            }
        }

        /* Right-aligned cluster: status · Preview|Live · toggles */
        Row {
            anchors.right: parent.right
            anchors.rightMargin: th.sp
            anchors.verticalCenter: parent.verticalCenter
            spacing: th.spHalf
            height: parent.height

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 220)
                elide: Text.ElideRight
                text: {
                    ws.stamp
                    return ws.hasBr() ? ws.br().statusText : ""
                }
                color: th.textFaint; font.pixelSize: th.fontSmall
            }

            HCheck {
                id: ghostChk
                text: "Ghost"
                onToggled: function(on) {
                    if (ws.hasBr()) ws.br().setCaseGhost(on)
                }
                function sync() {
                    value = ws.hasBr() ? ws.br().caseGhost : false
                }
            }
            HCheck {
                id: liveChk
                text: "Live"
                onToggled: function(on) {
                    if (ws.hasBr()) ws.br().setLive(on)
                }
                function sync() {
                    value = ws.hasBr() ? ws.br().live : false
                }
            }

            Item { width: th.spHalf; height: 1 }

            HBtn {
                text: "Insp"; w: 44
                visible: ws.narrow
                active: ws.inspOpen
                onClicked: ws.inspOpen = !ws.inspOpen
            }
            HBtn {
                text: "Diag"; w: 46
                active: ws.diagOpen
                onClicked: ws.diagOpen = !ws.diagOpen
            }
        }
    }

    /*======================= MIDDLE =======================*/
    Item {
        id: mid
        x: 0
        y: ws.hdrH
        width: ws.width
        height: ws.height - ws.hdrH - shelf.height - diag.height

        C.DeviceTree {
            id: devTree
            x: 0; y: 0
            width: ws.treeW; height: parent.height
            bridgeOverride: ws.bridgeOverride
        }

        /* Left splitter — drag resizes the device tree. */
        Splitter {
            id: splitL
            property int w0: 0
            x: devTree.width; y: 0; height: parent.height
            onPressed: w0 = ws.treeW
            onMoved: function(dx) {
                ws.treeW = Math.max(160, Math.min(380, w0 + dx))
            }
        }

        StudioScene {
            id: scene
            x: devTree.width + splitL.width
            y: 0
            width: parent.width - x
                   - (ws.narrow ? 0 : insp.width + splitR.width)
            height: parent.height
            /* The workspace docks the inspector — the floating M2
               overlay stays for standalone/test use only. */
            overlayInspector: false
            focusPeer: insp
        }

        /* Right splitter — docked inspector width (drag +x
           shrinks the panel). */
        Splitter {
            id: splitR
            property int w0: 0
            visible: !ws.narrow
            x: parent.width - insp.width - width
            y: 0; height: parent.height
            onPressed: w0 = ws.inspW
            onMoved: function(dx) {
                ws.inspW = Math.max(220, Math.min(480, w0 - dx))
            }
        }

        C.Inspector {
            id: insp
            width: ws.inspW
            height: parent.height
            y: 0
            /* Docked at wide widths; an overlay drawer (animated
               slide) under ~1040 px — spec §3 small-window rule. */
            x: !ws.narrow ? parent.width - width
                          : (ws.inspOpen ? parent.width - width
                                         : parent.width)
            visible: x < parent.width
            z: 5
            ctl: scene.editorCtl
            cam: scene.editorCam
            bridgeOverride: ws.bridgeOverride
            hostOverride: ws.hostOverride
            Behavior on x {
                enabled: ws.narrow
                NumberAnimation { duration: th.dur; easing.type: Easing.OutCubic }
            }
        }
    }

    /*======================= LOOK SHELF ===================*/
    C.LookShelf {
        id: shelf
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: diag.top
        height: implicitHeight
        bridgeOverride: ws.bridgeOverride
        hostOverride: ws.hostOverride
        Rectangle { anchors.top: parent.top; width: parent.width
                    height: 1; color: th.border }
    }

    /*=================== DIAGNOSTICS DRAWER ===============*/
    Rectangle {
        id: diag
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: ws.diagOpen ? ws.diagH : 0
        color: th.panel
        clip: true
        Behavior on height {
            NumberAnimation { duration: th.dur; easing.type: Easing.OutCubic }
        }
        Rectangle { anchors.top: parent.top; width: parent.width
                    height: 1; color: th.border }

        Column {
            anchors.fill: parent
            anchors.margins: th.sp
            spacing: th.spHalf

            /* Probe controls — every button lands on StudioTab's
               serialized worker path (studioHost seam), so flash/
               measure keep the exclusive pausePushes interlock. */
            Row {
                width: parent.width
                spacing: th.spHalf
                height: 26

                Text {
                    text: "Diagnostics"
                    color: th.text; font.pixelSize: th.fontTitle
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }

                ComboBox {
                    id: ctrlBox
                    height: 24; width: 220
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: ws.hasHost()
                    model: []
                    textRole: "label"
                    valueRole: "index"
                    function refresh() {
                        model = ws.hasHost()
                              ? ws.host().diagControllers() : []
                        refreshZones()
                    }
                    /* Zones follow the CURRENT controller — on
                       activation, on model-driven index resets, and
                       on Refresh (empty-model currentValue is
                       undefined -> zones clear instead of going
                       stale). */
                    function refreshZones() {
                        zoneBox.model = (ws.hasHost()
                                && ctrlBox.currentValue !== undefined)
                            ? ws.host().diagZones(ctrlBox.currentValue)
                            : []
                    }
                    onActivated: refreshZones()
                    onCurrentIndexChanged: refreshZones()
                    contentItem: Text {
                        text: ctrlBox.displayText
                        color: th.text; font.pixelSize: th.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8; elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: th.radiusSm; color: th.field
                        border.color: ctrlBox.visualFocus ? th.accent
                                                          : th.borderHi
                    }
                }
                ComboBox {
                    id: zoneBox
                    height: 24; width: 170
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: ws.hasHost()
                    model: []
                    textRole: "label"
                    valueRole: "index"
                    contentItem: Text {
                        text: zoneBox.displayText
                        color: th.text; font.pixelSize: th.fontSmall
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8; elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: th.radiusSm; color: th.field
                        border.color: zoneBox.visualFocus ? th.accent
                                                          : th.borderHi
                    }
                }

                HBtn {
                    text: "Refresh"; w: 62
                    enabled: ws.hasHost()
                    onClicked: {
                        ws.host().diagRefresh()
                        ctrlBox.refresh()
                    }
                }
                HBtn {
                    text: "Flash 4s"; w: 70
                    enabled: ws.hasHost()
                    /* Empty-model currentValue is undefined — marshal
                       nothing rather than a phantom 0 that could flash
                       zone 0 of the wrong controller. */
                    onClicked: {
                        if (ctrlBox.currentValue === undefined
                            || zoneBox.currentValue === undefined)
                            return
                        ws.host().diagFlash(ctrlBox.currentValue,
                                            zoneBox.currentValue)
                    }
                }
                HBtn {
                    text: "Latency"; w: 62
                    enabled: ws.hasHost()
                    onClicked: {
                        if (ctrlBox.count === 0)
                            return
                        ws.host().diagMeasure()
                    }
                }
                HBtn {
                    text: "Bindings"; w: 66
                    enabled: ws.hasBr()
                    onClicked: ws.logLine(ws.br().bindingReport())
                }

                Item { width: 4; height: 1 }

                Text {
                    visible: !ws.hasHost()
                    text: "host unavailable — probes disabled"
                    color: th.textFaint; font.pixelSize: th.fontSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            /* Log tail — binding reports, probe output, status
               messages land here (was the C++ results box). */
            Rectangle {
                width: parent.width
                height: parent.height - 26 - th.spHalf
                color: th.field; radius: th.radiusSm
                border.color: th.border
                clip: true
                Flickable {
                    id: logFlick
                    anchors.fill: parent
                    anchors.margins: 6
                    contentWidth: logText.width
                    contentHeight: logText.height
                    boundsBehavior: Flickable.StopAtBounds
                    Text {
                        id: logText
                        /* parent is the flickable's contentItem whose
                           width follows contentWidth — bind to the
                           flickable instead to avoid a loop. */
                        width: Math.max(implicitWidth, logFlick.width)
                        text: ws.diagLog === ""
                              ? "diagnostics output appears here — " +
                                "Refresh lists controllers; Bindings " +
                                "prints the binding report."
                              : ws.diagLog
                        color: ws.diagLog === "" ? th.textFaint : th.textDim
                        font.family: "Consolas"
                        font.pixelSize: th.fontSmall
                        wrapMode: Text.NoWrap
                    }
                    onContentHeightChanged: {
                        /* tail-follow */
                        if (contentHeight > height)
                            contentY = contentHeight - height
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        liveChk.sync()
        ghostChk.sync()
        if (ws.hasHost())
            ctrlBox.refresh()
    }
}
