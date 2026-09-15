# Desktop Lighting Studio Next Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the selected polished 2.5D desk editor with attractive device graphics, editable placement/orientation, middle-button panning, reusable JSON presets, and complete JSON configuration.

**Architecture:** Retain the native plugin and existing effects/output core. Add a shared scene graph and transform contract, transactional JSON document store, editor commands, stable presentation models, and a unified QML workspace.

**Tech Stack:** C++17, Qt 6.8.3, Qt Quick / Quick 3D / Quick Controls, qmake, nlohmann JSON, OpenRGB plugin API 5, MSVC 2022.

**Spec:** [Studio Next design](../specs/2026-09-15-studio-next-design.md).

**Status:** Proposed staged plan. The visual direction is selected; detailed design remains reviewable. No implementation or deployment is included in this planning task. Each milestone has an independently testable deliverable; expand its checks into a focused implementation checklist at execution time.

## Global Constraints

- Keep C++17, Qt 6.8.3, qmake, and OpenRGB plugin API 5 for the first release.
- Keep the Qt-free scene/effect core.
- `studio.json` is the authoritative active workspace file.
- Hierarchy and output ownership are separate relationships.
- A drag is one undo command, regardless of pointer event count.
- Middle-button drag pans the camera; it never moves a device.
- Preview bloom is a display effect and does not increase physical output brightness.
- Resizing a visual preset never resizes a hardware zone implicitly.
- Live output is off on first launch and imported presets cannot enable it.
- Preserve the existing address verification, binding identity, mirrored output, and transport pacing behavior.

## Execution map

All paths below are relative to `plugins/DesktopLightingStudio/` unless prefixed `tests/` or `docs/`. New paths are proposed, not existing APIs. Avoid broad refactoring beyond the responsibilities named here.

| Milestone | Main additions | Existing integration points | Exit demonstration |
|---|---|---|---|
| 1. Foundations | `scene/SceneGraph.*`, `config/StudioConfig.*`, `config/ConfigStore.*`, `config/ConfigMigration.*`, `schemas/` | `SceneTypes.*`, `SceneJson.*`, `SceneBridge.*`, `EffectEngine.cpp` | Legacy desk opens in v2 JSON with unchanged output addresses and correct transforms |
| 2. Editing | `editor/EditorController.*`, `editor/TransformCommands.*`, `editor/SceneObjectModel.*`, `ui/editor/` | `SceneBridge.*`, `StudioScene.qml` | Move/rotate mouse and case, middle-pan, undo, save/reopen |
| 3. Graphics | `ui/StudioWorkspace.qml`, `ui/components/`, `ui/devices/`, `ui/materials/`, `assets/` | `StudioTab.*`, `StudioScene.qml`, `studio.qrc`, `.pro` | Attractive, coherent desk editor at multiple display scales |
| 4. Devices | `presets/DevicePreset.*`, `presets/PresetRegistry.*`, `presets/devices/`, `ui/library/` | `DefaultDesk.*`, `EmitterLayout.*`, binding UI, store | Edit a fan preset in UI and JSON, instantiate and export it |
| 5. Effects | `effects/EffectJson.*`, `presets/effects/`, `ui/effects/` | `Presets.*`, `EffectTypes.*`, `SceneBridge.*` | Recreate current nine looks, edit layers and save a personal look |
| 6. Release | Fixture scenes, Qt integration tests, performance report, packaging manifest | build/deploy scripts, output lifecycle, docs | Fresh-install, recovery, performance and hardware acceptance pass |

## Milestone 1 — reliable scene and JSON foundation

### Task 1.1: One transform graph for renderer and effects

**Files:** modify `scene/SceneTypes.*`, `scene/SceneJson.*`, `scene/DefaultDesk.cpp`, `effects/EffectEngine.cpp`, `plugin/SceneBridge.*`, `ui/StudioScene.qml`; create `scene/SceneGraph.*`; extend `tests/studio_scene_test.cpp`.

**Contract:** `parent_id` establishes local placement; body `size_m` is separate from dimensionless positive scale. Stored XYZ angles use the existing C++ rotation convention, converted into the quaternion bound to the QML node. Resolved world transforms feed both effects and key-event origins.

- [ ] Add core fixtures for combined X/Y/Z rotations, scaled children, translated groups, missing parents, cycles, and mirrored output ownership.
- [ ] Add a Qt integration fixture comparing C++ emitter world positions to `Node.mapPositionToScene()` for mixed-axis rotations; tolerance is 0.0001 m.
- [ ] Implement graph validation, shared rotation conversion, world-transform resolution, and distinct geometry dimensions.
- [ ] Convert default mouse and case parts into groups while preserving their world placement, stable IDs, and all output mappings.
- [ ] Update effects and key lookup to consume the resolved graph; verify moved keyboard ripples originate at the moved keys.
- [ ] Run core and transform integration tests; review the change before building editor gestures.

### Task 1.2: Authoritative versioned workspace

**Files:** create `config/StudioConfig.*`, `config/ConfigStore.*`, `config/ConfigMigration.*`, `schemas/studio.schema.json`, `tests/studio_config_test.cpp`; modify `scene/SceneJson.*`, `plugin/SceneBridge.*`, `.pro`.

**Contract:** validation returns field-specific errors and a complete candidate document. The store changes active state only after success. Persist settings through one store; the old host settings are a migration source only.

- [ ] Capture a legacy scene/input fixture and test migration of IDs, positions, colors, mirrored ownership, effect state and input settings.
- [ ] Test malformed types, duplicate IDs, cycles, invalid scales, out-of-range LED addresses, newer versions, save failures and interrupted-save recovery.
- [ ] Implement root configuration and schema, candidate validation, migration backup and atomic save; route all existing persistent settings through it.
- [ ] Add dirty status, debounced autosave/recovery, and external-change detection with explicit conflict choices.
- [ ] Expose Open config folder, Reload JSON and Save As. Open/reload failures must leave current inputs and live output state untouched.
- [ ] Round-trip the full migrated fixture and verify there are no hidden setting changes outside `studio.json`.

**Milestone exit:** the current desk survives migration and save/reopen, with preview/effect transform parity and recoverable edits.

## Milestone 2 — direct manipulation

### Task 2.1: Editor state and undoable transforms

**Files:** create `editor/EditorController.*`, `editor/TransformCommands.*`, `editor/SceneObjectModel.*`, `tests/studio_editor_test.cpp`; modify `plugin/SceneBridge.*`, `.pro`.

**Contract:** begin gesture snapshots selected local transforms; update gesture previews; commit pushes one command; cancel restores the snapshot. A committed model change invalidates affected world transforms and key lookup, not unrelated delegates.

- [ ] Test a 100-update drag produces one undo command; cancelled/no-op drags produce none; undo/redo restores every affected child.
- [ ] Implement selection, locked nodes, local/world transforms, multi-selection pivot, plane constraints, snapping and numeric edits.
- [ ] Implement grouping, rename, visibility, undoable deletion, and explicit mirrored duplication without extra output ownership.
- [ ] Replace transform updates through whole-list `sceneChanged` with granular model notifications; verify stable delegates during drag.

### Task 2.2: Camera, tools and inspector

**Files:** create `ui/editor/CameraController.qml`, `ui/editor/SelectionController.qml`, `ui/editor/TransformGizmo.qml`, `ui/editor/TransformInspector.qml`, `tests/editor_qml/`; modify `ui/StudioScene.qml`, `ui/studio.qrc`.

- [ ] Add input tests for middle-pan over both hardware and empty space; assert object transforms do not change.
- [ ] Add orthographic desk view, Top/Front/Case views, pointer-centered zoom, frame selection and pan fallback.
- [ ] Implement Move/Rotate handles, left-drag plane intersection, click-versus-drag threshold, snapping, Escape cancellation and keyboard focus rules.
- [ ] Connect numeric position/orientation fields, align/distribute, lock/hide, and explicit Paint mode.
- [ ] Manually exercise each interaction from the design table at 100%, 150% and 200% scaling.

**Milestone exit:** arrange a complete mouse and a populated case, rotate both, undo, save and reopen; middle-button pan always works independently.

## Milestone 3 — coherent modern graphics

### Task 3.1: Unified workspace

**Files:** create `ui/StudioWorkspace.qml`, `ui/components/Theme.qml`, `ui/components/DeviceTree.qml`, `ui/components/Inspector.qml`, `ui/components/LookShelf.qml`; modify `plugin/StudioTab.*`, `ui/studio.qrc`, `.pro`.

- [ ] Define shared spacing, colors, typography, icons, focus states and motion settings.
- [ ] Build header, searchable hierarchy, contextual inspector, shelf and diagnostics drawer around the existing viewport.
- [ ] Preserve all current playback/input/identification actions while moving them into the new controls.
- [ ] Verify small-window layout, sidebar resizing, keyboard focus, empty states, missing hardware and dirty/save feedback.

### Task 3.2: Device families and lighting

**Files:** create `ui/devices/{Keyboard,Mouse,Fan,Ram,Case,Gpu,Monitor,Strip}.qml`, `ui/materials/StudioEnvironment.qml`, `assets/`; modify `ui/StudioScene.qml`, `ui/studio.qrc`, preset geometry dispatch and deployment resources.

- [ ] Build accurate generic silhouettes and LED surfaces for each family; maintain canonical dimensions, origins and emitter coordinates.
- [ ] Add environment lighting, antialiasing, contact shading, restrained bloom and clear selection treatment.
- [ ] Add quality tiers and optional perspective presentation view; share the exact same LED color buffer between tiers.
- [ ] Capture consistent before/after images of desk, keyboard, mouse and case views; inspect highlight clipping, glass picking and readability.
- [ ] Measure Balanced performance against the fixture before enabling costly settings by default.

**Milestone exit / first release candidate:** a substantially improved desk editor with usable transforms and JSON persistence. Review this experience before expanding authoring features.

## Milestone 4 — reusable device presets

**Files:** create `presets/DevicePreset.*`, `presets/PresetRegistry.*`, `presets/devices/*.device.json`, `schemas/device.schema.json`, `ui/library/{DeviceLibrary,DevicePresetEditor}.qml`, `tests/device_preset_test.cpp`; modify `scene/DefaultDesk.*`, `scene/EmitterLayout.*`, configuration definitions and deployment resources.

- [ ] Test ring/strip/matrix/point generation, invalid zone sizes, duplicate preset IDs, missing assets, revision snapshots and LED ordering.
- [ ] Implement schema-backed registry and embedded definition snapshots. Instantiation gives each node and binding a fresh stable instance ID.
- [ ] Replace hardcoded default device descriptions with packaged JSON, retaining a minimal recoverable fallback desk if data is missing.
- [ ] Add search, thumbnails, categories, favorites, drag/add to desk, Save variant and Create preset from selection.
- [ ] Build visual geometry/material/zone/layout editor with immediate virtual preview and explicit local hardware binding.
- [ ] Implement relative-asset export and explicit preset-update preview. Test editing the library cannot silently mutate an existing workspace.

**Milestone exit:** create a 120 mm fan preset, reverse its LED order, save it as JSON, add two instances, and reopen/export without recompiling.

## Milestone 5 — editable lighting looks

**Files:** create `effects/EffectJson.*`, `presets/effects/*.effect.json`, `schemas/effect.schema.json`, `ui/effects/{LayerStack,PaletteEditor,EffectInspector}.qml`, `tests/effect_json_test.cpp`; modify `effects/Presets.*`, `effects/EffectTypes.*`, bridge and document store.

- [ ] Capture deterministic frame fixtures from the existing nine looks at fixed times/seeds before changing their representation.
- [ ] Implement data-driven preset loading over existing effect primitives and bounded deterministic remix parameters.
- [ ] Compare JSON-backed looks against captured frames, including synthetic audio/key/screen inputs.
- [ ] Add layer order, enabled state, blend, opacity, palette, targets, local/world space and spatial origin/path editing with undo.
- [ ] Surface input requirements and disconnected-input states beside each effect. Keep source toggles and parameters in the same persisted document.
- [ ] Save a personal look, restart, and verify layer state, seed, colors, target selection and playback behavior.

**Milestone exit:** users can edit and share complete looks in the UI or JSON without modifying `Presets.cpp`.

## Milestone 6 — release quality

**Files:** extend `tests/` with integration projects and fixture scenes; modify `plugin/SceneBridge.*`, `plugin/StudioTab.*`, output coordination and build/deploy scripts where checks identify gaps; add `docs/studio-next-acceptance.md` and user authoring documentation.

- [ ] Add build-only validation and explicit resource packaging so code checks do not inadvertently deploy over the running host.
- [ ] Run core, config, preset, editor and Qt transform/input integration suites on the pinned toolchain.
- [ ] Measure p95 frame times and drag latency for the normal and stress fixtures; record hardware, resolution, quality and sample duration.
- [ ] Verify no needless preview rendering when hidden; verify live effect playback continues as requested.
- [ ] Exercise save recovery, preset import errors, external-edit conflicts, fresh install and v1 upgrade using temporary configuration roots.
- [ ] Review worker ownership and shutdown in the bridge and diagnostics; serialize identify/live operations through shared output coordination and test close/reconnect during work.
- [ ] Complete the previously pending physical audio/key/screen checks; test mirrored fans, slow I2C devices and wireless pacing after scene edits.
- [ ] Perform the under-two-minute setup usability task; correct discoverability issues found in observation.
- [ ] Package QML modules, schemas, presets, assets, documentation and license notices; test host load from a fresh install.

**Milestone exit:** every criterion in design section 8 has recorded evidence, or an explicitly documented limitation accepted before release.

## Verification entry points

The current Qt-free suite is `tests/build-studio-tests.bat`. It builds scene, effect and input tests. Extend its source list for Qt-free additions, and add separate Qt Test projects for editor/config/QML integration. Run intentional failing behavioral checks before their implementation and rerun after the change.

The current `plugins/DesktopLightingStudio/build-plugin.bat` also deploys. Add a build-only path before using it for routine validation. Packaging and host/hardware checks are distinct from pure test runs. This planning task has not run or claimed these checks.

## Priority and scope control

**First:** correct transforms, middle-pan, move/rotate, grouping, persistent JSON, unified UI, recognizable devices.

**Next:** visual device preset authoring, editable effect layers, robust import/export.

**Then:** measured performance, onboarding, packaging and compatibility evidence.

Defer room simulation, a node graph, timeline, arbitrary shaders and an online marketplace. These do not block the requested editor experience and would delay the first coherent release.
