# Stage 1 — accurate desk and static color control

Date: 2026-09-15. Branch: `desktop-lighting-studio`.
Parent plan: `docs/2026-09-15-desktop-lighting-studio-plan.md`.
Stage 0 record: `docs/2026-09-15-stage0-feasibility.md`.

**Status:** verified working in the elevated host (2026-09-15). Orbit,
zoom, selection, per-emitter painting, and live output all confirmed —
including the three Lian Li SL Wireless fans after their zone map
(`Fan 1-3`, 40 LEDs each) was confirmed.

Fixes landed during verification:

- `OrbitCameraController.origin` must be a `Node` (camera re-parented
  under an orbit origin); a `vector3d` left the controller dead.
- `Qt.keyboardModifiers()` does not exist — replaced with paired
  `TapHandler`s using `acceptedModifiers` (Shift paints, plain selects).
- Ghost bodies (case shell) are non-pickable so interior picks land;
  each LED got an invisible ~2.6x pick proxy.
- `PushZone` lazily switches zones/devices into a per-LED mode before
  writing — hardware only has to be in Direct/Custom on first push.
- DIMM bindings disambiguate by I2C location (0x19/0x1B); Lian Li
  bindings match `UNI FAN` + vendor `Lian Li`.
- Smart App Control blocks fresh DLL hashes at random — relink until
  `NativeLibrary.Load` passes, then xcopy (see Stage 0 doc).

## Scope for this stage

From the parent plan's Stage 1 gate:

- Editable recognizable geometry: keyboard, mouse, case shell, fans, pump,
  RAM, verified GPU lighting.
- Placement, transparent case view, picking, device binding, LED-order
  calibration, linked-group display, undo/redo.
- Static color, palette painting, brightness, virtual preview, explicit
  live output.
- Persist layouts; reload correctly with devices enumerated in a
  different order.

Gate: selecting any confirmed part changes the correct real lights;
mirrored copies stay identical; unverified lights cannot receive writes;
layout survives restart.

## Scene model (`scene/` — Qt-free, deterministic)

Right-handed coordinates, Y up, meters. The scene document is the source
of truth for object placement and emitter positions; QML renders from it
and the output adapter writes from the same document.

- `Vec3`, `Transform` (position / euler degrees / scale).
- `Emitter` — one addressable LED: local position, `group` id, address
  within its bound zone, `mirror_of` for linked copies.
- `EmitterLayout` — parametric generators: `Ring(n, radius, start_angle,
  reversed, face_y)`, `Strip(n, spacing)`, `Matrix(rows, cols, pitch)`,
  and `KeyboardMatrix` driven by the zone's matrix map. `face_y` lifts a
  ring onto the light-bearing face so emitter dots aren't sealed inside
  the opaque body mesh.
- `SceneObject` — id, kind (`decor`, `device`, `linked`), transform,
  `binding` ref, `verified` flag (false → no writes, ever), emitters.
- `DeviceBinding` — persistent identity: controller name + vendor +
  serial/location + zone name + expected LED count. Never a controller-
  list index.
- `SceneDocument` — versioned JSON (`"version": 1`), objects, named
  selections, colors. `FromJson`/`ToJson`, forward-compatible field
  tolerance.
- `BindingResolver` — matches bindings to live `RGBControllerInterface`s
  by identity; reports `resolved / unresolved / ambiguous` per binding.
  Ambiguous identical devices must never silently pick one.

Mirroring semantics (plan §hardware model): all copies of a mirrored
output reference one logical emitter set, sampled in a shared group
frame, receiving identical colors. `mirror_of` copies render the same
group state at their own transform; they have no output address.

## Default desk scene

Built from `docs/hardware-inventory.md` + `configs/zone-imports/`:

| Object | Binding | Emitters |
|---|---|---|
| G512 keyboard | zone 0 (matrix map) | 107 per-key |
| Basilisk V3 Pro | wheel/logo/strip zones | per zone counts |
| 4× case fans | ARGB_V2_1 | one 8-LED ring, 3 mirror copies |
| Cooler radiator ×3 | ARGB_V2_2 seg 0–7 | one 8-LED ring, 2 mirror copies |
| Cooler pump | ARGB_V2_2 seg 8–15 | 8-LED ring on pump block |
| Top-GPU fan | ARGB_V2_3 | 8-LED ring |
| 3× SL Wireless | runtime zones | per-fan rings when detected |
| 2× Corsair DIMM | DRAM zones | 10-LED strips |
| GPU side logo | GPU logo zone | logo cluster |
| GPU fan rings / top logo | — | unverified → not bound, shown grey |
| Case shell, desk | — | decor only |

`LED_C` / chipset accents stay out until physically located (plan rule).

## Output adapter (`output/`)

- Resolves bindings against `api->GetRGBControllers()` on demand and on
  `ResourceManagerUpdated`.
- Writes only mapped addresses inside bound zones; checks
  `MODE_FLAG_HAS_PER_LED_COLOR` on the active zone mode first —
  unwritable zones report a reason, never resize zones.
- Serializes writes on the existing `g_io_mutex`; one worker push per
  change set (static colors need no streaming loop yet).
- Live output is an explicit toggle, default off — preview only until
  enabled.

## UI

- `SceneBridge` (QObject, plugin side) exposes the document to QML:
  object list, per-object emitter positions/colors, selection, color
  and brightness setters, live toggle, undo/redo (`QUndoStack`), save /
  load to the plugin config dir.
- `ui/StudioScene.qml` rebuilds the desk from `objectList`: decor
  geometry, case shell with transparency toggle, per-object emitter
  dots driven by bridge colors, pick → select.
- StudioTab keeps the Stage 0 device-inspection bar (still needed for
  calibration measurements) plus: object picker fallback, color swatch,
  brightness slider, Live checkbox, Save/Load, Undo/Redo, case-alpha
  toggle.

## Tests (`tests/studio_scene_test.cpp`)

Deterministic, no hardware: transform math, ring/strip/matrix layouts,
reversed + start-angle ring ordering, mirrored copies share group color
and own no address, JSON round-trip + version tolerance, binding
resolution under reordered / missing / ambiguous controllers,
unverified objects refuse writes. Built by
`tests/build-studio-tests.bat` with plain `cl` (Qt-free headers only).

## Explicitly deferred

- Drag-to-place / resize editing in 3D (transforms are editable data
  now; direct-manipulation gizmos come with placement UI).
- LED-order physical calibration (ring start angle / reverse fields are
  in the model; the guided chase-light calibration UI comes after the
  first live session).
- Per-emitter instancing optimization if ~200 emitter Models prove
  slow.
- Audio/key/screen inputs (Stage 3), effect layers (Stage 2).
