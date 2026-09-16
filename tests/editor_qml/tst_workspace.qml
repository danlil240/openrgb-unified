/*---------------------------------------------------------*\
|||  tst_workspace.qml                                      ||
|||                                                         ||
|||  Studio Next task 3.1 shell coverage — pure QML via    ||
|||  the signed qmltestrunner. Components under test take  ||
|||  the bridgeOverride/hostOverride seams (same pattern   ||
|||  as SelectionController) since qmltestrunner has no    ||
|||  C++ context properties. Asserts:                      ||
|||    - every new shell file compiles                     ||
|||    - the device tree follows the object model          ||
|||    - shelf buttons invoke bridge slots                 ||
|||    - the inspector follows the bridge selection        ||
|||    - StudioWorkspace instantiates + the diagnostics    ||
|||      drawer opens                                      ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtTest

TestCase {
    id: tc
    name: "WorkspaceShell"
    when: windowShown

    /*---------------- stubs ----------------*/
    function makeBridge() {
        return {
            log: [],
            /* scene */
            selectedId: "",
            selectedInstances: [],
            dirty: false,
            documentPath: "/tmp/studio.json",
            statusText: "stub ok",
            canUndo: false,
            canRedo: false,
            live: false,
            caseGhost: false,
            paintColor: "#ff0000",
            /* effects */
            playing: false,
            activePreset: "",
            effectSpeedPct: 100,
            effectIntensityPct: 100,
            brightnessPct: 100,
            presetList: [
                { id: "calm",  name: "Calm",  description: "d" },
                { id: "storm", name: "Storm", description: "d" }
            ],
            /* inputs */
            audioInput: false,
            keyInput: false,
            screenInput: false,
            /* tree data (flat rows like objectList) */
            objectList: [
                { id: "kbd", instancePath: "kbd", label: "Keyboard",
                  kind: "device", bound: "ok", visible: true,
                  locked: false, verified: true, emitters: 104,
                  dx: 0.36, dy: 0.03, dz: 0.13,
                  bx: 0.36, by: 0.03, bz: 0.13 },
                { id: "kbd/keys", instancePath: "kbd", label: "Keys",
                  kind: "device", bound: "ok", visible: true,
                  locked: false, verified: true, emitters: 104,
                  dx: 0, dy: 0, dz: 0, bx: 0, by: 0, bz: 0 },
                { id: "mouse", instancePath: "mouse", label: "Mouse",
                  kind: "device", bound: "unresolved", visible: true,
                  locked: false, verified: false, emitters: 3,
                  dx: 0.12, dy: 0.04, dz: 0.06,
                  bx: 0.12, by: 0.04, bz: 0.06 },
                { id: "fan_r", instancePath: "fan_r", label: "Fan R",
                  kind: "linked", bound: "none", visible: true,
                  locked: true, verified: false, emitters: 8,
                  dx: 0, dy: 0, dz: 0, bx: 0, by: 0, bz: 0 }
            ],
            /* slots the shell calls */
            select: function(o) {
                this.log.push("select:" + o)
                this.selectedId = o
                var s = o.indexOf("/")
                var inst = s < 0 ? o : o.substring(0, s)
                this.selectedInstances = [inst]
            },
            selectInstance: function(i, add) {
                this.log.push("selectInstance:" + i)
                if (add) {
                    if (this.selectedInstances.indexOf(i) < 0)
                        this.selectedInstances.push(i)
                } else {
                    this.selectedInstances = [i]
                }
            },
            clearEditorSelection: function() { this.selectedInstances = [] },
            setInstanceVisible: function(i, on) {
                this.log.push("vis:" + i + ":" + on)
            },
            setInstanceLocked: function(i, on) {
                this.log.push("lock:" + i + ":" + on)
            },
            renameInstance: function(i, n) {
                this.log.push("rename:" + i + "->" + n)
            },
            groupSelected: function()      { this.log.push("group") },
            ungroupSelected: function()    { this.log.push("ungroup") },
            deleteSelected: function()     { this.log.push("delete") },
            duplicateMirrored: function()  { this.log.push("mirror") },
            gestureActive: function()      { return false },
            undo: function()               { this.log.push("undo") },
            redo: function()               { this.log.push("redo") },
            /* effects */
            playPreset: function(p)        { this.log.push("playPreset:" + p)
                                             this.activePreset = p
                                             this.playing = true },
            setPlaying: function(on)       { this.log.push("setPlaying:" + on)
                                             this.playing = on },
            stopEffect: function()         { this.log.push("stop")
                                             this.playing = false },
            remix: function()              { this.log.push("remix") },
            setEffectSpeedPct: function(v)     { this.log.push("speed:" + v) },
            setEffectIntensityPct: function(v) { this.log.push("intensity:" + v) },
            setBrightnessPct: function(v)      { this.log.push("bright:" + v) },
            /* inputs */
            setAudioInput: function(on)    { this.audioInput = on },
            setKeyInput: function(on)      { this.keyInput = on },
            setScreenInput: function(on)   { this.screenInput = on },
            setScreenIndex: function(i)    { this.log.push("screen:" + i) },
            screenIndex: function()        { return 0 },
            audioSensitivityPct: function(){ return 100 },
            rippleDecayPct: function()     { return 100 },
            reducedMotion: function()      { return true },
            /* inspector data */
            instanceState: function(id) {
                return { id: id, type: "stub-type",
                         x: 0.1, y: 0.0, z: 0.2,
                         rx: 0, ry: 45, rz: 0,
                         visible: true, locked: false }
            },
            objectInfo: function(id) {
                return { label: id, kind: "device", bound: true,
                         writable: true, reason: "" }
            },
            bindingReport: function() { return "OK kbd -> stub" }
        }
    }

    function makeHost() {
        return {
            log: [],
            uiSave: function()        { this.log.push("save") },
            uiSaveCopyAs: function()  { this.log.push("saveAs") },
            uiReload: function()      { this.log.push("reload") },
            uiReset: function()       { this.log.push("reset") },
            uiRestoreBackup: function(){ this.log.push("backup") },
            uiOpenWorkspaceFolder: function() { this.log.push("folder") },
            uiPickColor: function()   { this.log.push("color") },
            uiScreenNames: function() { return ["Display 1", "Display 2"] },
            diagControllers: function() {
                return [ { index: 0, label: "[0] Stub Ctrl" } ]
            },
            diagZones: function(c) {
                return [ { index: 0, label: "0: zone0 (8 LEDs)" } ]
            },
            diagRefresh: function()      { this.log.push("drefresh") },
            diagFlash: function(c, z)    { this.log.push("flash:" + c + "/" + z) },
            diagMeasure: function()      { this.log.push("measure") }
        }
    }

    function logged(log, prefix) {
        return log.filter(function(s) { return s.indexOf(prefix) === 0 })
    }

    /*---------------- compile smoke ----------------*/

    function test_shell_files_compile() {
        var ui = "../../plugins/DesktopLightingStudio/ui/"
        var files = [ "StudioWorkspace.qml",
                      "components/Theme.qml",
                      "components/DeviceTree.qml",
                      "components/DeviceLibrary.qml",
                      "components/Inspector.qml",
                      "components/LookShelf.qml" ]
        for (var i = 0; i < files.length; i++) {
            var c = Qt.createComponent(Qt.resolvedUrl(ui + files[i]))
            compare(c.status, Component.Ready,
                    files[i] + ": " + c.errorString())
        }
    }

    function test_theme_tokens() {
        var c = Qt.createComponent(Qt.resolvedUrl(
                    "../../plugins/DesktopLightingStudio/ui/components/Theme.qml"))
        compare(c.status, Component.Ready, c.errorString())
        var t = c.createObject(tc)
        verify(t !== null)
        compare(t.sp, 8)                       /* 8 px rhythm  */
        verify(t.radius >= 8 && t.radiusLg <= 12)
        verify(t.dur >= 120 && t.dur <= 180)   /* spec motion  */
        /* Status chips are text+color pairs. */
        compare(t.statusText("device", "ok"), "Connected")
        compare(t.statusText("device", "unresolved"), "Missing")
        compare(t.statusText("device", "none"), "Unmapped")
        compare(t.statusText("linked", "ok"), "Mirrored")
        t.destroy()
    }

    /*---------------- DeviceTree ----------------*/

    function test_tree_rows_follow_model() {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/DeviceTree.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        var stub = makeBridge()
        /* NOTE: stubs are assigned AFTER createObject — passing a
           JS object through the initial-property bag converts it
           and drops the function members. */
        var tree = comp.createObject(tc)
        verify(tree !== null)
        tree.bridgeOverride = stub
        tree.width = 240
        tree.height = 300
        var list = null
        /* treeList is an internal id — reach it through the tree's
           children via the exposed ListView objectName pattern:
           walk children recursively for a ListView. */
        function findList(o) {
            if (o === null || o === undefined)
                return null
            if (o.objectName === "treeList")
                return o
            for (var i = 0; i < o.children.length; i++) {
                var r = findList(o.children[i])
                if (r)
                    return r
            }
            return null
        }
        list = findList(tree)
        verify(list !== null, "treeList not found")
        compare(list.count, stub.objectList.length,
                "tree rows must follow the model")
        waitForRendering(list)

        /* Click a row -> bridge.select with the row's object id. */
        var row = list.itemAtIndex(2)   /* "mouse" */
        verify(row !== null)
        row.activate(0)
        verify(logged(stub.log, "select:mouse").length === 1,
               "row click did not reach bridge.select")

        /* Search filters the rows. */
        tree.search = "mouse"
        var shown = 0
        for (var i = 0; i < list.count; i++) {
            var r = list.itemAtIndex(i)
            if (r && r.shown)
                shown++
        }
        compare(shown, 1, "search should leave only the mouse row")
        tree.search = ""

        tree.destroy()
    }

    /*---------------- LookShelf ----------------*/

    function test_shelf_calls_bridge() {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/LookShelf.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        var stub = makeBridge()
        var shelf = comp.createObject(tc)
        verify(shelf !== null)
        shelf.bridgeOverride = stub
        shelf.width = 900

        /* Find a shell button: matching text AND a clicked signal. */
        function findBtn(o, want) {
            if (!o)
                return null
            if (o.text === want && typeof o.clicked === "function")
                return o
            for (var i = 0; i < o.children.length; i++) {
                var r = findBtn(o.children[i], want)
                if (r)
                    return r
            }
            return null
        }
        var playBtn = findBtn(shelf, "Play")
        verify(playBtn !== null, "Play button missing")
        playBtn.clicked()
        verify(logged(stub.log, "playPreset:calm").length === 1,
               "Play did not start the first preset")

        /* Pause path. */
        playBtn.clicked()
        verify(logged(stub.log, "setPlaying:false").length === 1)

        /* Preset card click plays that preset. */
        var list = null
        function findObj(o, name) {
            if (!o) return null
            if (o.objectName === name) return o
            for (var i = 0; i < o.children.length; i++) {
                var r = findObj(o.children[i], name)
                if (r) return r
            }
            return null
        }
        list = findObj(shelf, "presetRow")
        verify(list !== null)
        compare(list.count, 2)
        waitForRendering(list)
        var card = list.itemAtIndex(1)
        verify(card !== null)
        card.activate()
        verify(logged(stub.log, "playPreset:storm").length === 1)

        shelf.destroy()
    }

    /*---------------- Inspector ----------------*/

    function test_inspector_follows_selection() {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/Inspector.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        var stub = makeBridge()
        var insp = comp.createObject(tc)
        verify(insp !== null)
        insp.bridgeOverride = stub
        insp.hostOverride = makeHost()
        insp.width = 300
        insp.height = 600

        compare(insp.targetId(), "")
        stub.selectedInstances = ["mouse"]
        insp.selStamp++          /* stub has no NOTIFY — bump */
        compare(insp.targetId(), "mouse",
                "inspector must follow selectedInstances")
        insp.destroy()
    }

    /*---------------- StudioWorkspace ----------------*/

    function test_workspace_instantiates() {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/StudioWorkspace.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        var wsx = comp.createObject(tc)
        verify(wsx !== null, "StudioWorkspace failed to instantiate")
        wsx.bridgeOverride = makeBridge()
        wsx.hostOverride = makeHost()
        wsx.width = 1280
        wsx.height = 720
        verify(wsx.treePanel !== null)
        verify(wsx.shelfPanel !== null)
        verify(wsx.inspPanel !== null)

        /* Diagnostics drawer: closed -> open animates to diagH. */
        compare(wsx.diagDrawer.height, 0)
        wsx.diagOpen = true
        tryCompare(wsx.diagDrawer, "height", 190, 1000)

        /* Narrow -> inspector becomes a drawer offscreen. */
        wsx.width = 900
        verify(wsx.narrow)
        wsx.inspOpen = true
        tryCompare(wsx.inspPanel, "x", 900 - wsx.inspW, 1000)

        wsx.destroy()
    }
}
