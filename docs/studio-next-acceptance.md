# Desktop Lighting Studio Next — acceptance record

One row per release criterion in
[`superpowers/specs/2026-09-15-studio-next-design.md`](superpowers/specs/2026-09-15-studio-next-design.md)
§8. Each row records the evidence gathered on branch `studio-next`
(milestones 1–6, commits `a225dd3..8d4f92f`) and a verdict:

- **PASS** — executable evidence exists and is recorded below.
- **LIMITATION** — the criterion cannot be fully closed by machine
  checks on this machine; the gap, rationale, and the exact manual
  procedure that would close it are documented in the limitation
  register. Accepted before release per the milestone-6 exit.

Toolchain for all machine evidence: Windows 11, MSVC 2022, Qt 6.8.3
(`win64_msvc2022_64`), plugin built via
`plugins/DesktopLightingStudio/build-only.bat` (**BUILD OK, not
deployed** — no validation step touches the running host).

## Machine-checkable suite totals (task 6.2)

| Suite | Result |
| --- | --- |
| `tests/studio_scene_test` | 274 checks, 0 failures |
| `tests/studio_config_test` | 114 checks, 0 failures (fixtures + v1/v2 migration incl. effect layers) |
| `tests/studio_editor_test` | 470 checks, 0 failures |
| `tests/effect_json_test` | 330 checks, 0 failures (144 frame-parity cases, bit-identical vs captured frames) |
| `tests/device_preset_test` | 308 checks, 0 failures (incl. oversize-bundle refusal) |
| `tests/studio_config_store_test` | 101 checks, 0 failures (incl. fresh-install writability) |
| QML suite (`tests/editor_qml`, qmltestrunner) | 86 passed, 0 failed |
| `tests/studio_transform_test` | 4 checks, 0 failures (Qt integration fixture, run at M1) |
| `tests/studio_perf` | PERF OK — numbers below |
| `plugins/DesktopLightingStudio/verify-package.bat` | PACKAGE OK — 74/74 manifest entries present in `out\` |
| `tests/build-only.bat` | BUILD OK (not deployed) |

SAC/WDAC caveat: this machine's Smart App Control policy
(VerifiedAndReputable, CodeIntegrity event 3077) denies freshly linked
unsigned executables by hash. The suites above were run green on real
binaries (per-suite delete+relink retries); two harnesses —
`tests/studio_lifecycle_test` and the `tests/editor_qml` C++ harness —
compile and link clean but could not be executed on this box. That is
a limitation row, not a pass.

## Acceptance table

### 1. Under-two-minute usability

> "A new user can add a device preset, move it, rotate it, choose a
> look, and save in under two minutes in a short observed usability
> session."

**Verdict: LIMITATION.** No observed human session has been run —
every step is machine-verified (add instance, transform, look
selection, save are all covered by the suites above) but the criterion
explicitly requires an *observed* session. See L1 for the procedure.

### 2. Grouped devices and transform parity

> "A mouse moves as one device with wheel/logo/underglow attached.
> Case children follow their parent. Rotated/scaled preview emitter
> world positions match effect samples within 0.1 mm."

**Verdict: PASS.**

- The mouse is a single `mouse-3zone` device instance whose wheel /
  logo / strip are zones of one type
  (`presets/devices/mouse-3zone.device.json`); the case and its parts
  are a `pc-case` type plus `parent`-linked instances — M1 converted
  both to the shared hierarchy preserving stable IDs and output
  mappings (`studio_scene_test`, 274 checks: graph validation,
  missing-parent/cycle rejection, mirrored output ownership).
- Preview/effect parity: `tests/studio_transform_test` renders the
  same fixture through a real `Node` tree and compares
  `Node.mapPositionToScene()` against the core's resolved world
  transforms — tolerance **0.0001 m**, covering mixed-axis
  rotation + scale, plus a default-desk emitter-parity check
  (4 checks, 0 failures). Both renderer and `EffectEngine` consume
  the same resolved graph, so the 0.1 mm bound holds by construction.

### 3. Middle-drag pan and gesture predictability

> "Middle drag reliably pans from both empty space and a device; no
> accidental selection, painting, or movement. Gesture cancellation
> and undo are predictable."

**Verdict: PASS.**

- `studio_editor_test` (470 checks): a 100-update drag produces
  exactly one undo command; cancelled and no-op drags produce none;
  gesture isolation, lock cascade, undo-during-gesture refusal,
  cancel restores the begin-gesture snapshot.
- QML suite (86 passed): `tst_controllers.qml` covers middle-pan over
  hardware and empty space with object transforms asserted unchanged,
  Escape cancellation, click-vs-drag threshold, and Paint-mode gesture
  exclusion.

### 4. Save/reopen fidelity and save safety

> "Save/reopen preserves all persistent controls, transforms,
> mappings, definitions, palettes, layers, and input choices. Invalid
> JSON and interrupted saves do not lose the last valid document."

**Verdict: PASS.**

- `studio_config_test` (114 checks): v3 round-trip of devices,
  bindings, `device_settings`, colors, inputs, `effects` (incl. the
  inline `layers` stack), meta; v1→v3 and v2→v3 migration keeps
  placement, bindings, colors, effect targets *and* the inline layer
  stack; both committed fixture scenes re-validate and re-resolve
  against the packaged type library on every run.
- `studio_config_store_test` (101 checks): `QSaveFile` atomic rename,
  last-valid `studio.backup.json`, interrupted-save recovery,
  debounced autosave + recovery prompt (an autosave identical to disk
  does not prompt), fresh-install materialization of schemas and both
  preset sets — owner-writable, user edits survive re-run —
  `WriteEffectFile` round-trip, oversize `studio.json` refused.
- `device_preset_test` (308 checks): malformed device/effect JSON,
  `../` path escapes, per-file and total byte caps, dependency
  closure — all rejected at inspect with field-specific errors.
- A failed load reports file + field path and leaves scene, inputs,
  and output untouched (candidate-then-activate in
  `config/ConfigStore`).

### 5. Frame-time target, normal fixture

> "Target p95 frame time ≤16.7 ms on the current desktop at a
> 1920 × 1080 viewport, Balanced quality, with a reproducible
> 30-device/1,000-emitter fixture. Measure drag response separately,
> targeting ≤50 ms."

**Verdict: PASS for engine cost; LIMITATION for render-loop FPS.**

Measured by `tests/studio_perf` (Qt-free `cl /O2`, `steady_clock`,
600 frame samples + 60 transform-commit samples, look `aurora`, seed
1337, 3 layers; fixture `tests/fixtures/scenes/normal/studio.json` —
30 placements, 63 resolved objects, 977 emitters):

- frame-eval **p95 0.196 ms** (p50 0.189, max 0.275) vs the 16.7 ms
  budget — ~85× headroom.
- transform-commit total (op + re-resolve) **p95 0.064 ms**
  (max 0.130) vs the 50 ms drag budget.

These are engine CPU + resolve costs only — not QtQuick3D render-loop
FPS at 1920×1080 on the real GPU path. That remaining measurement is
L2.

### 6. Stress-scene editability

> "A 100-device/5,000-emitter stress scene remains editable at
> ≥30 fps in Low quality; if the asset budget misses this, reduce
> draw calls/detail before adding effects."

**Verdict: LIMITATION.**

- Fixture reality: `tests/fixtures/scenes/stress/studio.json` is 100
  placements / 203 resolved objects / **3,809 emitters**, built from
  the packaged type library by the checked-in generator
  (`tests/scene_fixture_gen.cpp`). The ~5,000-emitter target is not
  reachable from packaged types — the densest shipped type is 40
  LEDs (`fan-slw`). The composition is real, recorded honestly.
- Engine evidence: frame-eval p95 **1.14 ms** (max 1.86),
  transform-commit total p95 **0.215 ms** (max 0.327) at 3,809
  emitters — the engine is far inside the 30 fps budget; what is
  unmeasured is the actual QtQuick3D render loop on the stress scene
  in Low quality. See L3.

### 7. Hidden/minimized rendering

> "Hidden/minimized Studio stops unnecessary preview rendering while
> requested live playback continues; idle rendering uses on-demand
> updates."

**Verdict: LIMITATION — mechanism implemented and audited, executable
proof SAC-blocked.**

Implemented in `SceneBridge::setPreviewVisible` (commit `a84e073`),
driven by `StudioTab` show/hide + `QWindow::visibilityChanged`:
hidden + live keeps evaluating and pushing hardware but skips the
per-tick `emittersChanged` repaint; hidden + live-off stops
`play_timer` entirely; re-showing repaints once. `playing &&
(visible || live)` is re-evaluated at every switch so the states
cannot drift.

`tests/studio_lifecycle_test` contains the exact slots for this
(hidden preview keeps pushing while `emittersChanged` goes silent;
play timer idles hidden+live-off and re-arms). The harness builds and
links clean; execution is denied by SAC/WDAC on this machine. See L4.

### 8. Disconnect/reconnect, close, load cycles

> "Device disconnect/reconnect, plugin close, and repeated load
> cycles finish without dangling worker callbacks or corrupted state.
> Identification uses the same output coordination as live
> playback."

**Verdict: LIMITATION — audit and fixes landed, executable proof
SAC-blocked, plus physical checks pending.**

Code evidence (task 6.1 audit, commits `a84e073`/`9a852b1`): every
push/lane/static/probe worker is counted into `push_workers` /
`probe_workers` and joined in `~SceneBridge`/`~StudioTab`; all worker
bodies are `try/catch`'d with flags cleared on every exit;
`live_output` is atomic (no `BlockingQueuedConnection` deadlock);
`probe_serial` serializes probe spans with synchronous live restore;
`controller_epoch` aborts probes holding stale controller pointers
on rescan; `ScopeExit`/`ProbeGuard` make mode/color restores
exception-safe. Diagnostics (`diagFlash`/`diagMeasure`) hold the same
two lane mutexes (`io_mutex` + `fast_io_mutex`) as live pushes via
`pausePushes()` — identification *is* the same output coordination.
Residual risks are documented in the task-6.1 report (epoch check →
`SetColor` window; wedged-driver join bound).

`tests/studio_lifecycle_test` covers stop→drain→restart, probes while
live, overlapping probes, destruction mid-write, `pausePushes()==false`
after shutdown, driver-throw recovery, rescan pointer swap, and 8×
create/destroy cycles — build/link verified only. See L4 (harness
execution) and L5 (physical disconnect/reconnect).

### 9. Stage-3 physical hardware checks

> "Recheck the existing stage-3 audio/key/screen hardware checks:
> the recorded report still lists physical verification as pending."

**Verdict: LIMITATION.** `docs/2026-09-15-stage3-inputs.md` still
carries its pending list verbatim — it needs the running elevated
host plus eyes on hardware. See L6 for the procedure. The Qt-free
input logic is covered (`build-studio-tests.bat` input suites,
178 checks at stage 3; the reactive presets are also covered by the
144 `effect_json_test` parity cases under synthetic audio/key/screen
inputs).

### 10. Fresh install / upgrade completeness

> "Fresh install and upgrade include required QML modules, assets,
> schemas, defaults and license notices. Test the exact
> host/toolchain combination; wider upstream/platform support is a
> separate release commitment."

**Verdict: PASS for the payload and materialization; LIMITATION for a
real fresh-install host load.**

- `plugins/DesktopLightingStudio/PACKAGING.md` records the full
  manifest: plugin DLL, Qt runtime DLLs, windeployqt QML set
  **including `QtQuick3D/Helpers`** (the module whose absence ships a
  black viewport), platform/style/image/TLS/network plugins, the
  `:/studio` QRC payloads (3 schemas, 13 device types, 9 looks, the
  `ui/` QML tree), the loose `ui/` override dir, and license
  coverage.
- `verify-package.bat`: **74/74** manifest entries present in `out\`,
  `QtQuick3D.Helpers` confirmed at `out/qml/QtQuick3D/Helpers`.
- `studio_config_store_test` fresh-install case loads the `:/studio`
  resources end-to-end against a temp workspace and asserts the
  materialized schemas/presets are owner-writable and user edits
  survive re-run (this test found — and 6.2 fixed — the qrc
  read-only copy bug).
- v1 host-settings migration and v2→v3 workspace upgrade are covered
  by `studio_config_test` (legacy blob → compact v3, `live_on_startup`
  stays false, `effects.layers` preserved).
- Remaining gaps: a real deploy into a clean host profile, a fresh
  `windeployqt` against a clean `out/`, and an install on a
  SAC-managed machine. See L7.

## Limitation register

### L1 — Observed two-minute usability session

Closes criterion 1. Requires a human who has not used Studio Next.

Procedure:

1. Build + deploy the plugin (`build-plugin.bat`) on the target
   machine; start OpenRGB elevated with a clean or existing profile.
2. Open the Studio tab and hand the user this script verbatim:
   "Add a device to your desk, move it, rotate it, pick a lighting
   look, and save."
3. Observe silently. Pass = all five actions complete in under two
   minutes without assistance (expected path: Library tab → drag or
   ✚ a type onto the desk → left-drag in Move mode → Rotate tool or
   `E` → look card on the shelf → header **Save**).
4. Record time, wrong turns, and any discoverability failures; file
   findings before release.

### L2 — Render-loop FPS on the real GPU path

Closes the render half of criterion 5. Engine CPU is measured (row
5); the QtQuick3D render loop is not.

Procedure: deploy the plugin, load
`tests/fixtures/scenes/normal/studio.json` as the workspace, set
Balanced quality at a 1920×1080 viewport, and measure sustained
frame time/FPS during playback and during a device drag (Qt render
stats or an external frame-time tool). Pass = p95 frame ≤16.7 ms and
drag response ≤50 ms.

### L3 — Stress-scene render FPS, Low quality

Closes criterion 6.

Procedure: load `tests/fixtures/scenes/stress/studio.json`
(100 devices / 3,809 emitters), set **Low** quality, and measure
sustained FPS while dragging a device. Pass = ≥30 fps and the scene
stays editable. If it misses, the spec's own remedy applies: reduce
draw calls/detail before adding effects.

### L4 — SAC-blocked test harnesses

Closes the executable evidence for criteria 7 and 8 (and the C++
slots behind the QML suite). Smart App Control / WDAC on this machine
denies freshly linked unsigned test binaries by hash (CodeIntegrity
3077); the copy-to-evaluated-path trampoline is dead — verdicts are
hash-bound. Nothing was simulated.

Procedure: on a machine where SAC permits local dev binaries (or
after the org policy evaluates the hashes):

```bat
tests\studio_lifecycle_test\build.bat    :: 11 test functions
tests\editor_qml\build.bat               :: C++ harness slots
```

Both already compile and link clean here; only execution is blocked.

### L5 — Physical disconnect/reconnect and pacing on real hardware

Closes the hardware half of criterion 8 and the plan's
"mirrored fans, slow I2C devices and wireless pacing after scene
edits" check. The hardware on this box (`docs/hardware-inventory.md`)
covers each case: ARGB_V2_1 is a 4-fan mirrored splitter; the
Corsair DIMMs ride SMBus (slow I2C); the Lian Li SL Wireless TX/RX
dongles exercise wireless pacing.

Procedure:

1. During live playback, unplug and replug a controller (e.g. the
   G512); confirm the device drops to Missing and rebinds without a
   restart, no stuck output, no crash.
2. With live playback running, close the Studio tab / unload the
   plugin; reopen. Repeat several cycles; watch the diagnostics log
   for the "waiting on workers" warnings and confirm shutdown is
   bounded.
3. Edit the scene (move the mirrored fan group, rebind a zone) and
   confirm the mirrored fans still share output, the SMBus DIMMs
   don't stall playback, and the wireless fans hold their pacing.

### L6 — Stage-3 physical audio/key/screen checks

Closes criterion 9 (pending list from
`docs/2026-09-15-stage3-inputs.md`).

Procedure, on the elevated running host:

1. Enable **Audio** in the shelf Inputs row, play music — onset
   rings visible on the desk (`shockwave` look); silence decays back
   to the base layer.
2. Enable **Keys**, type on the G512 — ripples originate at the
   correct physical key (`keyripple` look); unmapped keys fall back
   to the preset origin.
3. Enable **Screen**, put a saturated test image on the chosen
   display — `ambient` follows the image; with capture unavailable
   the base layer remains.
4. Toggle each source off/on — threads join cleanly; burst typing
   does not grow the event queue.

### L7 — Real fresh-install host load

Closes the deployment half of criterion 10.

Procedure:

1. `build-plugin.bat` against a clean `out/` (fresh `windeployqt`),
   then `verify-package.bat` (expect 74/74).
2. Deploy into a clean Windows profile's OpenRGB install; launch the
   host; confirm the plugin loads, the workspace dir materializes all
   three schemas + `presets/devices` (13 types) + `presets/effects`
   (9 looks), the scene renders (no missing QML module errors), and
   Live is off on first launch.
3. Repeat on a SAC-managed machine — unsigned-DLL policies may gate
   the plugin itself; record the outcome.
