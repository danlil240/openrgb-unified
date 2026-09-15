# Desktop Lighting Studio Next — product and architecture design

Date: 2026-09-15

Status: proposed upgrade; user selected a polished 2.5D desk editor. This document defines the remaining design for review, not completed functionality.

## 1. Product direction

Make Studio a beautiful, direct-manipulation lighting workspace: arrange a recognizable version of your desk, choose a look, adjust it visually, and save everything in readable JSON. Everyday editing should feel closer to arranging objects in a design tool than operating hardware diagnostics.

Keep the existing native OpenRGB plugin and Qt Quick 3D renderer. Use an orthographic, angled desk camera by default, with Top, Front, Case, and optional perspective views. “2.5D” describes the editing experience; physical device positions and spatial effects remain three-dimensional.

### Approaches considered

| Approach | Benefit | Tradeoff |
|---|---|---|
| **Polished 2.5D editor on existing Qt Quick 3D — selected direction** | Detailed hardware, easy placement, reuses the existing engine | Requires disciplined input handling and a proper scene hierarchy |
| Full 3D room editor | Maximum camera and environment freedom | More navigation, asset work, and rendering cost before everyday editing improves |
| Flat 2D editor | Simple and inexpensive to render | Loses useful case depth and physical orientation cues |

The quality goal is measurable usability, visual consistency, and reliability. “Best OpenRGB plugin” is an aspiration, not a claim of an established ranking. Effects and Visual Map already cover substantial effect and mapping functionality; Studio should distinguish itself through integrated device modeling, direct editing, and portable documents. See [Effects](https://openrgb.org/plugin_effects.html) and [Visual Map](https://openrgb.org/plugin_visual_map.html).

## 2. Current code and gaps

Reviewed workspace commit `16fcc90`, OpenRGB submodule `0b365446`.

| Area | Existing foundation | Upgrade needed |
|---|---|---|
| Rendering | `ui/StudioScene.qml`, Quick 3D scene and LED preview | Primitive bodies, no antialiasing, basic lighting, small selection marker |
| Editing | Selection, paint, brightness, undo for existing color operations | No transform editing API, no grouped devices, no gizmos or layout tools |
| Scene | `scene/SceneTypes.*`, positions, Euler rotations, scale, bindings | Explicit hierarchy, dimensions, locked state, consistent renderer/engine transforms |
| Persistence | `scene/SceneJson.cpp`; scene and inputs stored through OpenRGB settings in `SceneBridge.cpp` | Dedicated editable JSON file, strict validation, migrations, recovery, portable presets |
| Devices | `scene/DefaultDesk.cpp`, emitter layout generators | Reusable JSON device definitions and an in-app preset editor |
| Effects | Nine registered presets, compositing engine, audio/key/screen input | JSON effect definitions, visible layer stack, palettes and target editing |
| UI | `plugin/StudioTab.cpp`, QWidget control rows around QQuickWidget | Unified QML workspace, contextual inspector, collapsible diagnostics |
| Output | Binding resolver, verified-address checks, paced output lanes | Preserve behavior through editing; coordinate identify operations with live output |

Two dependencies must be fixed before freely editable orientations:

- The C++ transform uses `Rz * Ry * Rx`; QML currently passes the same angles to `eulerRotation`, whose documented order is ZXY. Use one explicitly computed quaternion for both systems. [Qt Node rotation documentation](https://doc.qt.io/qt-6.8/qml-qtquick3d-node.html#eulerRotation-prop).
- `transform.scale` currently means dimensions for some decorative bodies, while device bodies use fixed QML sizes and emitter nodes do not apply that scale. Separate body dimensions from dimensionless object scale and verify identical world coordinates across preview and effects.

The mouse body, wheel, logo, and underglow are separate root objects today. They must become children of one mouse group. The same applies to the case and its components.

## 3. Workspace and visual design

```text
 Project / Saved status     Undo Redo     View     Preview | Live
 ┌───────────────────┬───────────────────────────┬────────────────────┐
 │ Devices / Library │                           │ Inspector          │
 │ Search            │      Large desk view      │ Position / Angle   │
 │ Desk              │                           │ Appearance / LEDs  │
 │   Keyboard        │   Move / Rotate / Snap    │ Binding / Effects  │
 │   Mouse           │                           │                    │
 │   Case            │                           │                    │
 ├───────────────────┴───────────────────────────┴────────────────────┤
 │ Looks: thumbnail cards          Play / Pause     Brightness        │
 └────────────────────────────────────────────────────────────────────┘
```

- Graphite surfaces, restrained accent color, clear type hierarchy, consistent icons, 8 px spacing rhythm, 8–12 px corners, visible keyboard focus.
- Let device lighting provide most of the color. Avoid persistent glowing borders or visual noise around controls.
- Resizable sidebars; at smaller widths the inspector becomes a drawer. Keep the viewport useful at a 1280 × 720 host window and 100%, 150%, and 200% display scaling.
- Device tree shows selection, visibility, lock state, and explicit Connected / Missing / Unmapped / Mirrored status. Color is accompanied by text or icons.
- Inspector follows selection. Basic editing shows position and angle first; dimensions, per-axis rotation, pivot, emitter mapping, and calibration live in expandable sections.
- Animate view transitions and panel feedback over roughly 120–180 ms. Direct dragging tracks the pointer without easing. Respect reduced motion.
- Move raw controller inspection, latency tools, and binding reports into a Diagnostics drawer.
- Add first-run guidance: choose a desk layout → match devices → arrange → choose a look. Offline preview remains fully editable.

### Device graphics

- Keyboard: shaped chassis, distinct keycaps, readable key positions, emissive legends or key surfaces.
- Mouse: curved shell, wheel, logo, separate underglow channel.
- Fans: square frame, blades, hub, segmented diffuser/ring; visible LEDs correspond to actual addressable emitters.
- RAM: heat spreader and continuous diffuser with underlying LED segments.
- Case: frame, glass, interior mounting cues, hideable panels and case-focused view.
- Generic fallback models for unsupported hardware; exact branded models are optional preset assets.
- Start with reusable geometry families and materials. Add packaged mesh assets where silhouettes require them; use consistent meters, origins, bounds, and asset license metadata.
- Selection uses an outline/bounds and clear handles. LED mapping mode enlarges address markers and labels their order.

### Rendering

Introduce physically based materials, environment lighting, soft contact shading, antialiasing, and restrained bloom. Qt 6.8 documents environment lighting, antialiasing, ambient occlusion, and extended glow effects; verify the chosen combination on the installed 6.8.3 host before making it the default. [Qt 6.8 scene environment](https://doc.qt.io/qt-6.8/qml-qtquick3d-sceneenvironment.html).

Offer Low, Balanced, and High quality presets. Balanced is the initial default. Decorative shadows and glow may change by tier; editor readability and actual LED colors must remain consistent. Preview bloom is a display effect and does not increase physical output brightness. Use a small lighting rig rather than one real light per LED.

## 4. Interaction contract

| Input | Default behavior |
|---|---|
| Left click | Select device/group; empty space clears selection |
| Left drag on selected device in Move mode | Move across the active plane, normally desk XZ, retaining height |
| Left drag on empty space | Marquee selection |
| **Middle-button drag** | **Pan the camera; never move a device** |
| Space + left drag | Pan alternative for trackpads/mice without a middle button |
| Alt + left drag | Orbit in optional free-view mode |
| Wheel | Zoom toward the pointer, with sensible minimum/maximum bounds |
| W / E | Move / Rotate tools when no text field has focus |
| Rotation ring | Rotate around the active plane normal; desk default is Y |
| Shift while transforming | Temporarily enable snapping: 10 mm translation, 15° rotation |
| F / Home | Frame selection / frame desk |
| Escape | Cancel current gesture and restore its starting transform |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |

Always expose tool buttons alongside shortcuts. Paint is an explicit mode; its pointer gestures cannot start movement or camera orbit. Retain Shift+click LED painting only within Paint mode to avoid competing with selection/snapping.

Numeric inspector: X/Y/Z displayed in millimeters, yaw/pitch/roll in degrees; saved positions use meters. Add drag-to-scrub values, reset per field, 90° rotation buttons, plane choices, local/world orientation, align, distribute, and snap-to-grid controls.

A drag is one undo command, regardless of pointer event count. Escape creates no history entry. Multi-selection transforms operate around a visible shared pivot. Moving a group updates its children; deleting a group shows which children will be removed and supports undo.

Duplicating a hardware-backed object creates a clearly labeled mirrored visual copy by default; it cannot silently introduce a second independent writer for the same LEDs. A separate Add device workflow creates an unbound instance for another physical device.

Camera movements are editor preferences, not scene-object history. Persist their final state after the gesture.

## 5. Scene model and architecture

Keep C++17, Qt 6.8.3, qmake, and OpenRGB plugin API 5 for the first release. Keep the Qt-free scene/effect core. Retain QQuickWidget as the plugin host; move primary controls into QML using Qt Quick Controls.

### Boundaries

- **Scene graph:** stable object IDs, parent IDs, local transforms, geometry dimensions, groups, emitters, bindings, visibility, lock state.
- **Transform math:** authored XYZ degree values retain the existing core convention; derive a normalized quaternion once and use it for rendering and world-space calculations. Matrix composition is `parentWorld × localTransform`.
- **Editor controller:** selection, tools, transform transactions, undo, alignment, grouping; does not write files or talk to hardware directly.
- **Document store:** whole-document validation, migrations, atomic saves, autosave/recovery, external-file change detection.
- **Preset registry:** read device/effect JSON libraries, instantiate immutable versions, expose searchable metadata.
- **Presentation models:** stable `QAbstractItemModel`/`QAbstractListModel` roles, granular changes for transforms and colors. Avoid rebuilding the entire scene on every drag tick.
- **Render components:** viewport, input controller, gizmos, device families, materials, environment, panels.
- **Existing effects/output:** consume the same evaluated scene transform graph. Rebuild key-event origin lookup when a keyboard or ancestor moves. Retain per-transport pacing and latest-frame coalescing.

Do not grow the already large `SceneBridge.cpp` and `StudioTab.cpp` into the entire editor. Extract only responsibilities this upgrade needs; preserve tested effect and device transport implementations.

Hierarchy and output ownership are separate relationships. `parent_id` determines placement; `mirror_of` determines shared color/output ownership. Mirrored fans remain identical even if their decorative copies move. Reject cycles and dangling references. Hiding a visual object does not mute hardware; Mute output is a separate labeled control.

## 6. JSON-first configuration and presets

**`studio.json` is the authoritative active workspace file:** UI preferences, camera, controls, render settings, inputs, output preferences, bindings, scene layout, active effects, and saved colors. Device and effect preset files form reusable libraries. Exported layout JSON files are optional reusable documents, not a second hidden settings store.

Store the workspace under OpenRGB's resolved user configuration directory in a `DesktopLightingStudio` subdirectory. Locate it through the host's available configuration API at implementation time. Provide Open config folder, Reload JSON, Save, Save As, Import, Export, and Restore backup actions. Avoid fixed usernames or repository paths in shipped defaults.

```text
DesktopLightingStudio/
  studio.json
  studio.backup.json
  studio.autosave.json
  presets/
    devices/*.device.json
    effects/*.effect.json
    layouts/*.layout.json
  assets/
  schemas/
```

Suggested top-level contract (illustrative valid JSON; an empty scene is allowed):

```json
{
  "$schema": "schemas/studio.schema.json",
  "schema_version": 2,
  "name": "My desk",
  "ui": { "theme": "graphite", "reduced_motion": false },
  "camera": { "view": "desk", "projection": "orthographic" },
  "controls": { "middle_drag": "pan", "move_snap_m": 0.01, "rotate_snap_deg": 15 },
  "render": { "quality": "balanced", "bloom": true },
  "inputs": { "audio": false, "keys": false, "screen": false },
  "output": { "brightness": 0.6, "live_on_startup": false },
  "scene": { "nodes": [], "bindings": [], "object_colors": {}, "emitter_colors": {} },
  "effects": { "playing": false, "layers": [], "seed": 42 },
  "definitions": { "devices": [], "effects": [] },
  "extensions": {}
}
```

Preset definitions describe device families, dimensions, asset references, materials, child parts, zone requirements, and emitter layout generators (`ring`, `strip`, `matrix`, `points`). Binding hints describe compatible hardware, not a user's serial number or automatically verified mapping. Presets cannot grant hardware verification.

Example device-preset shape:

```json
{
  "$schema": "../../schemas/device.schema.json",
  "schema_version": 1,
  "id": "generic.fan.120mm.8led",
  "revision": 1,
  "name": "120 mm RGB fan — 8 LEDs",
  "category": "fan",
  "geometry": { "family": "fan", "size_m": [0.12, 0.025, 0.12] },
  "appearance": { "body_color": "#202028", "roughness": 0.45 },
  "zones": [
    {
      "id": "ring",
      "led_count": 8,
      "layout": { "type": "ring", "radius_m": 0.052, "start_angle_deg": 0, "reverse": false }
    }
  ]
}
```

### Editing and compatibility rules

- Users can change ordinary preferences, device dimensions, palettes, mappings, and effect parameters through either the UI or JSON. Behavior implementations remain C++/QML; JSON is data, not executable code.
- Pretty-print two-space JSON with readable `#RRGGBB` colors, stable identifiers, documented units, and deterministic serialization. Convert existing packed colors on migration.
- Ship schemas and full examples; validate required types, finite coordinates, positive dimensions/scales, unique IDs, parent/mirror cycles, LED bounds, references, and known effect primitives before activating a document.
- Retain `extensions` fields; report unknown core fields as likely typos. Reject newer unsupported schema versions without rewriting them.
- Load into a temporary candidate. A failed load reports the file and field path and leaves current scene, inputs, and output state untouched.
- Save through `QSaveFile` or equivalent atomic replacement. Keep the last valid backup and a debounced recovery file. Save errors remain visible and never show “Saved.”
- Detect external edits. If the workspace is clean, offer Reload; if dirty, offer Keep current / Reload disk / Save current as copy. Never overwrite unsaved edits automatically.
- Import existing OpenRGB `DesktopLightingStudio` settings once when no dedicated workspace exists. Back up the original, migrate scene and input settings, preserve IDs, LED addresses and world placement, then use the new store exclusively. Do not restart migration after a user intentionally creates an empty scene.
- Scene documents embed the versions of device/effect definitions they use. Those snapshots are authoritative for that workspace. Library edits affect newly added devices; Apply preset update is explicit, undoable, and shows the differences.
- Export removes local device serial/location identifiers by default and uses relative packaged asset paths. Imported bindings require local resolution; shared presets never enable live output by themselves.
- Enforce file/object/emitter/asset-size limits and local asset paths in import validation; malformed community content must not destabilize the host.

## 7. Device and effect authoring

Device Library: search, category filters, thumbnails, favorites, Add to desk, Create preset from selection, Save as variant. Include generic keyboard, mouse, fan, RAM, strip, GPU, case and monitor families, plus verified presets for this setup where data exists.

Device Preset Editor: dimensions, parts, materials, zones, LED count/order, start angle, reversed direction, matrix mapping, per-emitter offsets, preview, and schema-backed export. Resizing a visual preset never resizes a hardware zone implicitly.

Effect shelf: migrate the existing nine looks into editable JSON definitions over existing engine primitives. Add palette swatches, speed, intensity, width, direction, targets, coordinate space, remix seed, and visible input requirements. Show a layer stack with order, enabled state, opacity and replace/add/screen blending. Put origins and paths directly in the viewport where applicable.

Prioritize editing the existing effects well before inventing another effect engine. Defer a node graph, keyframe timeline, online marketplace, cloud account, full room simulator, and arbitrary user shaders.

## 8. Release criteria

- A new user can add a device preset, move it, rotate it, choose a look, and save in under two minutes in a short observed usability session.
- A mouse moves as one device with wheel/logo/underglow attached. Case children follow their parent. Rotated/scaled preview emitter world positions match effect samples within 0.1 mm.
- Middle drag reliably pans from both empty space and a device; no accidental selection, painting, or movement. Gesture cancellation and undo are predictable.
- Save/reopen preserves all persistent controls, transforms, mappings, definitions, palettes, layers, and input choices. Invalid JSON and interrupted saves do not lose the last valid document.
- Target p95 frame time ≤16.7 ms on the current desktop at a 1920 × 1080 viewport, Balanced quality, with a reproducible 30-device/1,000-emitter fixture. Measure drag response separately, targeting ≤50 ms. These are proposed targets, not current measurements.
- A 100-device/5,000-emitter stress scene remains editable at ≥30 fps in Low quality; if the asset budget misses this, reduce draw calls/detail before adding effects.
- Hidden/minimized Studio stops unnecessary preview rendering while requested live playback continues; idle rendering uses on-demand updates.
- Device disconnect/reconnect, plugin close, and repeated load cycles finish without dangling worker callbacks or corrupted state. Identification uses the same output coordination as live playback.
- Recheck the existing stage-3 audio/key/screen hardware checks: the recorded report still lists physical verification as pending.
- Fresh install and upgrade include required QML modules, assets, schemas, defaults and license notices. Test the exact host/toolchain combination; wider upstream/platform support is a separate release commitment.

## 9. Delivery order

1. **Scene and JSON foundations:** consistent transforms, hierarchy, migration, transactional persistence.
2. **Direct editor:** middle-button pan, selection, move/rotate, numeric inspector, snapping, undo.
3. **Visual identity:** unified workspace, detailed devices, materials, lighting, quality tiers.
4. **Device library:** JSON presets, browser, visual preset editor, portable export.
5. **Effect authoring:** JSON looks, palettes, layers, spatial handles and input controls.
6. **Release quality:** usability, performance, recovery, hardware regression and packaging.

The first reviewable release combines milestones 1–3: a visibly upgraded desk that can be arranged, rotated, saved, and reopened. Milestones 4–6 turn that editor into a reusable, shareable plugin.
