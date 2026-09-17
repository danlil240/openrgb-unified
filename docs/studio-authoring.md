# Desktop Lighting Studio — authoring guide

How your desk, devices, and lighting looks are stored, and how to edit
them — in the UI or by hand. Everything Studio persists is plain JSON
you can read and diff.

## Where your files live

Studio keeps its workspace in a `DesktopLightingStudio` folder under
OpenRGB's user configuration directory (on Windows,
`%APPDATA%\OpenRGB\DesktopLightingStudio`). **File → Open Config
Folder** opens it.

```text
DesktopLightingStudio/
  studio.json                  the workspace — the file you edit
  studio.backup.json           last-known-good copy, refreshed on each save
  studio.autosave.json         debounced autosave; recovery source
  studio.v2.backup.json        pre-migration copy (only if you upgraded from v2)
  legacy-settings.backup.json  pre-migration copy (only if you upgraded from v1 host settings)
  migration.done               marker: "legacy migration already ran"
  presets/
    devices/*.device.json      device-type definitions
    effects/*.effect.json      lighting-look definitions
    layouts/                   reserved — nothing writes it today
  assets/                      bundle-referenced assets (appears on import)
  schemas/                     copies of the JSON schemas, for editors
```

On first launch Studio creates the workspace dir, `schemas/`,
`presets/devices/`, and `presets/effects/`, and materializes the
bundled content from inside the plugin: the three schemas, 13 device
types, and 9 looks. (`assets/` appears when you import a bundle that
carries assets; `presets/layouts/` is reserved in the spec — no code
writes it yet.) Files it creates are writable and *yours* — re-runs
never overwrite your edits.

## studio.json — the workspace (schema_version 3)

`studio.json` is the authoritative document. It stores *instances and
settings* — never expanded geometry. A minimal sketch:

```json
{
  "$schema": "schemas/studio.schema.json",
  "schema_version": 3,
  "name": "My desk",
  "devices": {
    "fan_front_top": {
      "type": "fan-120", "x": 0.38, "y": 0.30, "z": 0.05,
      "rx": 90, "ry": 0, "rz": 0
    }
  },
  "bindings": {},
  "device_settings": {},
  "colors": { "objects": {}, "emitters": {} },
  "effects": { "preset": "aurora", "seed": 42, "playing": true }
}
```

Top-level sections (all optional except `schema_version`):

| Section | Contents |
| --- | --- |
| `name` | Workspace display name. |
| `ui` | `theme`, `reduced_motion`, `favorites` (pinned device-type ids — library UI state, not part of the scene). |
| `camera` | `view` (`desk`/`top`/`front`/`case`/`free`), `projection` (`orthographic`/`perspective`), `target`, `yaw_deg`, `pitch_deg`, `distance`, `span`. Camera moves are editor preference, not undoable scene edits. |
| `controls` | `middle_drag` (`pan`/`orbit`), `move_snap_m` (default 0.01), `rotate_snap_deg` (default 15). |
| `render` | `quality` (`low`/`balanced`/`high`), `bloom`. Bloom is display-only — it never changes physical LED output. |
| `inputs` | `audio`, `keys`, `screen`, `screen_index`, `sens_pct` (25–200), `decay_pct` (50–300). |
| `output` | `brightness` (0–1), `live_on_startup` — defaults `false`; migration and imported documents can never turn it on. |
| `devices` | Root instances, keyed by your instance id → `{type, x, y, z, rx, ry, rz, parent?}`. `type` resolves to `presets/devices/<type>.device.json`. `x/y/z` are meters; `rx/ry/rz` are Euler degrees applied `Rz·Ry·Rx`. `parent` (another instance id) makes placement relative — cycles are rejected. |
| `bindings` | Hardware identities keyed by binding id: `controller_name`, `vendor`, `serial`, `location`, `device_type`, `zone_name`, `zone_leds`. These are persistent identities, never controller-list indices. |
| `device_settings` | Per-instance state keyed by instance path (`fan_front_top`, or `case/case_fans` for a nested child): `visible`, `locked`, `mirror_of`, and `zones` → `{binding, addr_base, verified}`. `mirror_of` shares another *same-type* instance's output (chains rejected). `verified` marks a hardware-checked binding — only Studio can set it; type files cannot grant it. |
| `colors` | `objects`: resolved object path → base color. `emitters`: object path → emitter index → painted color. Colors write as `"#RRGGBB"` (a packed `0x00BBGGRR` integer is still accepted from older files). |
| `effects` | `preset` (a look id), `seed` (0–4294967295), `speed`, `intensity`, `playing`, and `layers` — your authored inline layer stack (see below). |
| `extensions` | Third-party data, retained verbatim. |

Validation rules you'll hit: unknown fields are reported as likely
typos (warnings); malformed values fail the load with a field-path
error and your running scene is left untouched; newer schema versions
are rejected, not rewritten. Instance/type ids match
`^[A-Za-z0-9_-]+$`. Never put `entities`, `emitters`, or device
definitions inside `devices` — the compact format doesn't allow them
(the instance object accepts only the fields above).

## Device types — `presets/devices/*.device.json` (schema_version 1)

One file per reusable device type. The file's `id` must equal the
basename (`fan-120.device.json` → `"id": "fan-120"`), and a file with
the same id overrides the packaged default — that's how "edit a
bundled type" works: your copy in `presets/devices/` shadows the
built-in one.

```json
{
  "$schema": "../../schemas/device.schema.json",
  "schema_version": 1,
  "id": "fan-120",
  "name": "120 mm RGB fan — 8 LEDs",
  "category": "fan",
  "entities": {
    "body": {
      "geometry": "fan_body", "size_m": [0.0, 0.0, 0.0],
      "x": 0.0, "y": 0.0, "z": 0.0,
      "rx": 0.0, "ry": 0.0, "rz": 0.0,
      "zone": "ring"
    }
  },
  "zones": [
    {
      "id": "ring", "entity": "body", "led_count": 8,
      "layout": {
        "type": "ring", "radius_m": 0.052,
        "start_angle_deg": 0.0, "face_y_m": 0.016, "reverse": false
      }
    }
  ]
}
```

- **entities** — local parts keyed by entity id: `geometry` +
  `size_m` (meters; `0` falls back to the geometry's canonical axis)
  + local `x/y/z/rx/ry/rz`, optional `parent` (another entity id —
  local chain, cycles rejected), `zone` (the zone this entity
  carries), `appearance` (material hints, retained verbatim). An
  entity may instead be a **child-device reference** —
  `{ "type": "<other-type-id>", x/y/z/rx/ry/rz }` — for nested
  assemblies; recursive type dependencies are rejected.
- **zones** — emitter generators on an entity: `id`, `entity`,
  `led_count`, and `layout`:
  - `ring`: `radius_m`, `start_angle_deg`, `face_y_m`, `reverse`.
  - `strip`: `spacing_m`, `origin`. (`face_y_m` and `reverse` are
    ring-only — the parser reads them only in the ring branch.)
  - `matrix`: `rows`, `cols`, `pitch_x_m`, `pitch_z_m`, `origin`,
    `empty` (unmapped-cell sentinel), `map` (row-major index map).
    `"dynamic": true` rebuilds emitters from the *bound hardware
    zone's* matrix map at runtime — `keyboard-104` uses this, so the
    visual layout follows whatever keyboard is actually attached.
  - `points`: `points` (explicit `[x,y,z]` positions for irregular
    hardware — `fan-slw` and `mouse-3zone` use this), optional
    `addresses` (per-point LED addresses; default is the instance's
    `addr_base` + index).
- **binding_hints** — optional array of
  `{controller_name, vendor, zone_name}` describing compatible
  hardware. Informational only: it never contains serials and never
  grants `verified`. Unknown keys are rejected as typos.
- `x`/`y`/`z`/`rx`/`ry`/`rz` are plain scalar numbers. Only `size_m`,
  `origin`, and `points` entries take vec3 forms — `[x,y,z]`
  (preferred) or `{x,y,z}`.

Expanded objects get ids like `fan_front_top/diffuser` — that's the
path `colors` and `device_settings` use. Changing a type file changes
every instance of it on **File → Reload Device Types** (validated
first; a bad file leaves the current scene active and reports the
error). To change just one device, save a variant: a new type file
with a new id, then point that instance's `type` at it.

## Effect looks — `presets/effects/*.effect.json` (schema_version 1)

A look is a stack of parametric layers composited over your painted
colors. Same rule as devices: `id` must equal the file basename, and
your file shadows a packaged default. The nine bundled looks are
`ambient`, `aurora`, `chrome`, `comet`, `embers`, `keyripple`,
`portal`, `reactor`, `shockwave`.

```json
{
  "schema_version": 1,
  "id": "aurora",
  "name": "Aurora",
  "description": "Teal/violet ribbons sweeping the desk",
  "needs": "",
  "layers": [
    {
      "primitive": "static",
      "palette": [ { "pos": 0.0, "color": "#060C1A" } ]
    },
    {
      "primitive": "wave",
      "direction": { "remix_yaw": [0.55, 0.0, -0.6, -0.4, 0.4] },
      "scale":    { "remix": [0.35, 0.6] },
      "speed":    { "remix": [0.15, 0.3] },
      "density":  { "remix": [1.2, 2.2] },
      "phase":    { "remix01": true },
      "opacity": 0.95,
      "palette": [
        { "pos": 0.0,  "color": "#00C8B4" },
        { "pos": 0.35, "color": "#7040E0" },
        { "pos": 0.65, "color": "#081838" },
        { "pos": 0.85, "color": "#005A6E" }
      ]
    },
    {
      "primitive": "noise",
      "blend": "screen",
      "scale": 7.0,
      "direction": [0.12, 0.05, 0.08],
      "speed": 1.0,
      "density": 0.86,
      "seed": { "remix_u32": [0, 4294967295] },
      "opacity": 0.5,
      "palette": [
        { "pos": 0.0, "color": "#EBF5FF" }
      ]
    }
  ]
}
```

Top level: `id`, `name`, `description`, `needs` (`""`, `audio`,
`key`, or `screen` — the input the look requires; shown as a badge on
the look card), and `layers` (max 64). Unknown root keys are
tolerated; unknown *layer* keys are errors.

Layer fields — all optional; defaults in parentheses:

| Field | Values |
| --- | --- |
| `primitive` | `static`, `gradient`, `wave`, `pulse`, `comet`, `noise`, `spin`, `ripple`, `screenfield`, `level` (`static`) |
| `space` | `world` (shared desk frame) / `local` (per-object) (`world`) |
| `blend` | `replace`, `add`, `screen` (`replace`) |
| `opacity` | 0–1 coverage; the intensity slider multiplies on top (1) |
| `speed` | motion rate; the global speed slider multiplies on top (1) |
| `scale` | wavelength / ring spacing / noise frequency / spoke count / tail length / band width — must be > 0 (0.5) |
| `phase` | 0–1 starting offset (0) |
| `density` | sharpness / contrast / ripple decay per second (1) |
| `origin` | `[x,y,z]` — pulse/comet/spin center, ripple spawn, screenfield center ({0,0,0}) |
| `direction` | `[x,y,z]` — wave/gradient axis, noise wind ({1,0,0}) |
| `path` | comet waypoints, auto-closed polyline (max 256) |
| `palette` | stops `{pos, color}` — `pos` strictly increasing, `#RRGGBB` only (max 64). Omit entirely when the primitive never samples color; a present-but-empty palette is an error. |
| `targets` | object ids / geometry tags / emitter groups; empty = every device object (max 64) |
| `source` | `""` (all), `audio`, `key`, `screen` — which input events feed a ripple |
| `seed` | per-layer u32 variation (0) |
| `enabled` | `false` composites nothing — the layer-stack on/off (true) |

### Remix specs — deterministic variation from the seed

Scalar numeric fields (`opacity`, `speed`, `scale`, `phase`,
`density`), `origin`/`direction` vector elements, and palette stop
`pos` may each be a literal *or* one spec object with exactly one
key. `path` waypoint numbers are literals only, and `seed` takes a
u32 literal or `{"remix_u32":[lo,hi]}`. Each spec consumes exactly
one draw from the seed's random stream, in document order (layers
array order, then field order within each layer object):

| Shape | Draw |
| --- | --- |
| `{"remix":[lo,hi]}` | uniform `Range(lo,hi)` |
| `{"remix01":true}` | `Next01()` — 0..1 |
| `{"remix_angle":true}` | `Angle()` — random angle in radians |
| `{"remix_neg":[lo,hi]}` | `-Range(lo,hi)` |
| `{"remix_pick":[a,b]}` | `Next01()<0.5 ? a : b` |
| `{"remix_u32":[lo,hi]}` | integer draw — `seed` fields only; `[0,4294967295]` maps to `Next()` verbatim |
| `{"remix_yaw":[x,y,z,lo,hi]}` | `RotateYaw(Normalize({x,y,z}), Range(lo,hi))` — `direction` fields only, radians |

Same seed → same resolved look, every time. Change `effects.seed`
(or hit **Remix** on the shelf) for a new draw.

### The workspace `effects.layers` rule

`studio.json → effects.layers` is the *resolved* stack: literals
only — a remix spec there is a validation error. When `layers` is
non-empty it wins over `preset` at runtime; `preset` keeps the
provenance so the UI can distinguish "look: aurora" from
"inline (customized)". Editing a look in
the UI writes literals into `effects.layers`; **Save as look…**
bakes the stack into a `*.effect.json` look document; **Reset to
preset** clears the inline stack so the named look resolves again.

## Working in the UI

- **File menu** — Save, Save Copy As… (writes a copy; your session
  keeps editing `studio.json`), Reload studio.json, Export Bundle…,
  Import Bundle…, Reload Device Types, Reset to Default Desk, Open
  Config Folder, Restore Backup. The header shows `● unsaved` when
  the document is dirty; a failed save never says "Saved".
- **Left sidebar** — two tabs in one slot: the device tree (placed
  instances: selection, visibility, lock, Connected / Missing /
  Unmapped / Mirrored status) and the **device library** (search,
  category chips, favorites pinned first, `file|packaged` source
  badge). Drag a type onto the desk or use its ✚ button; **Save
  variant…** and **Create preset from selection…** mint new type
  files.
- **Device preset editor** — modeless panel (library row *Edit*,
  *Edit type* / *Edit as variant* on an instance, or new). Edits
  metadata, parts, materials, zones and all four layouts with a live
  preview resolved on a throwaway document — nothing touches your
  scene until Save writes `<id>.device.json` (validated,
  id==filename). Fields show millimeters; files store meters. Its
  binding section writes only `device_settings.zones` rows — a type
  edit never rebinds or resizes hardware.
- **Looks shelf** — look cards with a `needs` badge (`audio — off`,
  `key — disconnected`, …, always icon + words), Play/Pause, Stop,
  **Remix** (new seed draw), speed/intensity/brightness sliders, and
  a collapsible Inputs row (Audio + sensitivity, Keys, Screen +
  display picker, Decay).
- **Effect editor** — the shelf's **Edit** opens it: a layer stack
  (reorder, enable, blend, opacity, add/remove) plus a per-layer
  inspector (space, origin/direction, speed/scale/phase/density/seed,
  source, targets, palette stops, comet path — fields the current
  primitive doesn't read are hidden). Pick buttons arm a viewport
  pick for origins/path points; each pick lands as one undoable
  gesture. Footer: **Save as look…**, **Reset to preset**, Close.
- **Safety rails** — Live output is off on first launch and
  `live_on_startup` defaults false; imported presets and bundles
  never enable it. The **Live** checkbox in the header is the only
  switch that writes to hardware. Preview bloom is display-only.
  Duplicating a hardware device makes a mirrored visual copy
  (`mirror_of`), never a second writer for the same LEDs.

## Saving, recovery, and editing by hand

- Saves are atomic (`QSaveFile`): before each commit the previous
  file is copied to `studio.backup.json` *if it still parses* — the
  backup is always the last valid document. **File → Restore
  Backup** rolls back.
- `studio.autosave.json` is written ~2 s after your last edit and on
  clean shutdown. If it exists, parses, and differs from
  `studio.json` at launch, Studio offers **Recover unsaved
  changes?** — Yes restores, No discards the autosave. An autosave
  identical to disk never prompts.
- Editing `studio.json` in another editor while Studio runs triggers
  the file watcher. Clean document → you're offered a reload. Dirty
  document → three choices: **Keep current** (next save overwrites
  the disk change), **Reload disk**, or **Save copy…** (keep both).
  Unsaved edits are never silently overwritten.
- On invalid JSON the load fails with file + field-path errors and
  your running scene is untouched — fix and reload.
- Upgrades: a v1 host-settings blob migrates once (original in
  `legacy-settings.backup.json`, `migration.done` marker); a v2
  expanded workspace migrates in place to v3 (original in
  `studio.v2.backup.json`, type files extracted to `presets/devices/`).

## Sharing — export/import bundles

**File → Export Bundle…** writes a directory: `studio.json`
(always sanitized — serials and locations are stripped, with no
opt-out), one copy of
each referenced device type, transitive dependencies, and `assets/`.

**File → Import Bundle…** inspects first: each incoming type is
marked `new`, `identical`, or `conflict`. Identical files are reused;
a same-id/different-content type imports under a remapped id — your
local files are never overwritten silently. Malformed JSON, `../`
path escapes, and oversize files are refused. Note the disclosure on
overwriting into a folder that already has a bundle: stale
`*.device.json` files the bundle doesn't include may be pruned from
its `presets/devices` folder. Imported bindings need local
resolution and imports never enable live output.
