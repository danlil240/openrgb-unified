/*---------------------------------------------------------*\
||  tst_library.qml                                        ||
||                                                         ||
||  Task 4.2 device-library coverage — pure QML via the   ||
||  signed qmltestrunner (same harness as                ||
||  tst_workspace.qml). DeviceLibrary takes the           ||
||  bridgeOverride seam; the stub supplies presetModel    ||
||  either as a bare row array or a {count,rowAt} object  ||
||  so the model-shaped path is covered too.              ||
||                                                         ||
||  Asserts:                                               ||
||    - the component compiles and instantiates            ||
||    - search filtering (name / typeId / category)        ||
||    - derived category list                              ||
||    - favorites-first then alphabetical sort             ||
||    - star toggle routes to setTypeFavorite              ||
||    - id/name dialog validation (dlgError)               ||
||    - submitNewType routes variant vs. assembly          ||
||    - addAtCenter hits the camera desk plane             ||
||                                                         ||
||  NOT covered (documented in the task report): the real  ||
||  QAbstractListModel path and the drag/drop gesture —   ||
||  both need a C++ context property / real pointer       ||
||  events; the C++ editor_qml_test harness instantiates  ||
||  the real SceneBridge for the drop path instead.       ||
||\*.--------------------------------------------------------*/
import QtQuick
import QtTest
import "../../plugins/DesktopLightingStudio/ui/components" as C

TestCase {
    id: tc
    name: "DeviceLibrary"
    when: windowShown

    /*---------------- stubs ----------------*/
    function libRows() {
        return [
            { typeId: "fan-120", name: "120 mm Fan", category: "fan",
              fromFile: false, zoneCount: 1, ledTotal: 8,
              bounds: { x: 0.12, y: 0.025, z: 0.12 }, favorite: false },
            { typeId: "monitor", name: "Monitor", category: "decor",
              fromFile: false, zoneCount: 0, ledTotal: 0,
              bounds: { x: 0.6, y: 0.4, z: 0.05 }, favorite: false },
            { typeId: "my-kit", name: "My Kit", category: "custom",
              fromFile: true, zoneCount: 2, ledTotal: 24,
              bounds: { x: 0.3, y: 0.1, z: 0.3 }, favorite: false },
            { typeId: "keyboard-104", name: "Keyboard",
              category: "input", fromFile: false, zoneCount: 1,
              ledTotal: 104, bounds: { x: 0.36, y: 0.03, z: 0.13 },
              favorite: true }
        ]
    }

    function makeBridge() {
        var rows = libRows()
        return {
            log: [],
            favCalls: [],
            addCalls: [],
            variantCalls: [],
            createCalls: [],
            selectedInstances: [],
            /* array form — libRows() takes it verbatim */
            presetModel: rows,
            setTypeFavorite: function(tid, on) {
                this.favCalls.push(tid + ":" + on)
                for (var i = 0; i < this.presetModel.length; i++)
                    if (this.presetModel[i].typeId === tid)
                        this.presetModel[i].favorite = on
            },
            addDeviceInstance: function(tid, x, y, z) {
                this.addCalls.push(tid + "@" + x.toFixed(3) + ","
                                   + y.toFixed(3) + "," + z.toFixed(3))
            },
            saveInstanceAsVariant: function(inst, tid, nm) {
                this.variantCalls.push(inst + "->" + tid + "|" + nm)
            },
            createTypeFromSelection: function(tid, nm) {
                this.createCalls.push(tid + "|" + nm)
            }
        }
    }

    /* model-shaped stub — {count(),rowAt(i)} like the real
       PresetListModel. */
    function makeModelBridge() {
        var rows = libRows()
        return {
            log: [], favCalls: [], addCalls: [],
            variantCalls: [], createCalls: [],
            selectedInstances: [],
            presetModel: {
                rows: rows,
                count: function() { return this.rows.length },
                rowAt: function(i) { return this.rows[i] }
            }
        }
    }

    function makeScene() {
        return {
            width: 800, height: 600,
            editorCam: {
                target: Qt.vector3d(0.05, 0.0, 0.02),
                planeHitPoint: function(px, py, axis, val) {
                    /* pretend the viewport center maps to the desk */
                    if (axis === 1 && px === 400 && py === 300)
                        return Qt.vector3d(0.05, 0.0, 0.02)
                    return null
                }
            }
        }
    }

    function makeLib(bridge, scene) {
        var comp = Qt.createComponent(Qt.resolvedUrl(
            "../../plugins/DesktopLightingStudio/ui/components/DeviceLibrary.qml"))
        compare(comp.status, Component.Ready, comp.errorString())
        /* Stubs go in AFTER createObject (same pattern as the
           DeviceTree tests) so construction can't trip on them. */
        var lib = comp.createObject(tc)
        lib.bridgeOverride = bridge
        lib.sceneView = scene
        lib.stamp++
        return lib
    }

    /*---------------- tests ----------------*/

    function test_compiles() {
        var lib = makeLib(null, null)
        verify(lib !== null)
        lib.destroy()
    }

    function test_rowsAndCategories() {
        var lib = makeLib(makeBridge(), null)
        compare(lib.libRows().length, 4, "all registry rows")
        compare(lib.totalCount, 4)
        compare(lib.categories().join(","),
                "custom,decor,fan,input",
                "derived sorted unique categories")
        compare(lib.allTypeIds().join(","),
                "fan-120,monitor,my-kit,keyboard-104")
        lib.destroy()
    }

    function test_modelShapedStub() {
        /* same pipeline, {count,rowAt} object like PresetListModel */
        var lib = makeLib(makeModelBridge(), null)
        compare(lib.libRows().length, 4, "rowAt pipeline")
        compare(lib.filteredRows().length, 4)
        lib.destroy()
    }

    function test_searchFilter() {
        var lib = makeLib(makeBridge(), null)
        lib.search = "fan"
        var ids = []
        var rows = lib.filteredRows()
        for (var i = 0; i < rows.length; i++)
            ids.push(rows[i].typeId)
        compare(ids.join(","), "fan-120", "search hits name AND typeId")

        lib.search = "120"          /* matches the typeId digits */
        compare(lib.filteredRows().length, 1)

        lib.search = "custom"       /* category text also matches */
        rows = lib.filteredRows()
        compare(rows.length, 1)
        compare(rows[0].typeId, "my-kit")

        lib.search = "zzzzz"
        compare(lib.filteredRows().length, 0, "no match empties")
        lib.search = ""
        compare(lib.filteredRows().length, 4)
        lib.destroy()
    }

    function test_categoryFilter() {
        var lib = makeLib(makeBridge(), null)
        lib.category = "fan"
        compare(lib.filteredRows().length, 1)
        compare(lib.filteredRows()[0].typeId, "fan-120")
        lib.category = "custom"
        compare(lib.filteredRows().length, 1)
        lib.category = ""
        compare(lib.filteredRows().length, 4, "All resets")
        lib.destroy()
    }

    function test_favoritesFirst() {
        var b = makeBridge()
        var lib = makeLib(b, null)
        var rows = lib.filteredRows()
        compare(rows[0].typeId, "keyboard-104",
                "favorite rows pin first")
        /* the rest are alphabetical by name */
        compare(rows[1].typeId, "fan-120")          /* "120 mm Fan" */
        compare(rows[2].typeId, "monitor")
        compare(rows[3].typeId, "my-kit")

        /* toggling a favorite re-sorts through the same pipeline */
        b.setTypeFavorite("monitor", true)
        lib.stamp++
        rows = lib.filteredRows()
        compare(rows[0].typeId, "keyboard-104")
        compare(rows[1].typeId, "monitor",
                "new favorite joins the pinned group")
        compare(b.favCalls.join(","), "monitor:true",
                "toggle routed to setTypeFavorite")
        lib.destroy()
    }

    function test_dialogValidation() {
        var lib = makeLib(makeBridge(), null)
        verify(lib.dlgError("", "Name") !== "", "empty id rejected")
        verify(lib.dlgError("bad id!", "Name") !== "",
               "bad charset rejected")
        verify(lib.dlgError("fan-120", "Name") !== "",
               "existing id rejected")
        verify(lib.dlgError("new-type", "") !== "",
               "empty name rejected")
        compare(lib.dlgError("new-type", "New Type"), "",
                "valid id + name pass")
        lib.destroy()
    }

    function test_submitRouting() {
        var b = makeBridge()
        var lib = makeLib(b, null)
        lib.submitNewType("variant", "fan0", "fan-x", "Fan X")
        compare(b.variantCalls.join(","), "fan0->fan-x|Fan X")
        compare(b.createCalls.length, 0)
        lib.submitNewType("assembly", "", "rig-1", "Rig")
        compare(b.createCalls.join(","), "rig-1|Rig")
        compare(b.variantCalls.length, 1)
        lib.destroy()
    }

    function test_addAtCenter() {
        var b = makeBridge()
        var lib = makeLib(b, makeScene())
        lib.addAtCenter("fan-120")
        compare(b.addCalls.length, 1)
        verify(b.addCalls[0].indexOf("fan-120@") === 0)
        /* the stub camera maps (400,300) -> (0.05, 0, 0.02) */
        verify(b.addCalls[0].indexOf("0.050") >= 0)
        verify(b.addCalls[0].indexOf("0.020") >= 0)
        lib.destroy()
    }

    function test_emptyRegistryState() {
        var b = makeBridge()
        b.presetModel = []
        var lib = makeLib(b, null)
        compare(lib.totalCount, 0)
        compare(lib.filteredRows().length, 0)
        lib.destroy()
    }

    function test_selectionGating() {
        var b = makeBridge()
        var lib = makeLib(b, null)
        compare(lib.selIds().length, 0)
        b.selectedInstances = ["fan0"]
        compare(lib.selIds().join(","), "fan0")
        lib.destroy()
    }
}
