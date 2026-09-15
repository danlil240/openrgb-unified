# Stage 3 — reactive input and deeper composition

Date: 2026-09-15. Branch: `desktop-lighting-studio`.
Parent plan: `docs/2026-09-15-desktop-lighting-studio-plan.md`.
Stage 2 record: `docs/2026-09-15-stage2-effects.md`.

## Scope for this stage

From the parent plan's Stage 3 gate:

- Windows audio loopback with silence/device-change handling; no
  captured audio is stored.
- Optional key-trigger events with layout translation; only transient
  effect events are retained, never typed-text history.
- Optional screen sampling with display selection, smoothing, and a
  fallback when capture is unavailable.
- New presets: bass shockwave, key ripple, screen atmosphere.
- Timeline is deferred — layer controls remain sufficient.

Gate: sources disconnect cleanly; silence decays to the base layer;
input bursts stay bounded; all scenes remain editable and useful
without input sources.

## Input model

The engine stays pure: `Evaluate(doc, t, frame, input)` takes an
`InputState` snapshot built by the bridge — same as `t`, the caller
supplies it, so tests feed synthetic input. `nullptr` (or an empty
state) makes every reactive primitive emit zero coverage, so
nonreactive scenes are untouched.

- `InputEvent` — `{t, pos, has_pos, strength, source, code}`. `t` uses
  the bridge's `play_t` clock (events stamped while paused spawn on
  resume; `play_t` resets are handled by skipping negative ages).
  `has_pos=false` means "spawn at the layer's `origin`" — audio onsets
  use this so presets control where rings originate; key events get a
  real position resolved bridge-side.
- `screen_cells` — smoothed ColorF grid (12x6) sampled by the
  `screenfield` primitive; empty grid = capture unavailable.
- `audio_level` — smoothed 0..1 loudness for the `level` primitive.

`inputs/InputBus` is the Qt-free thread-safe hub: providers push
events (bounded deque, cap 96, stale entries pruned past ~6 s —
input bursts stay bounded by construction), post the screen grid, and
update the audio level. The bus stamps event times through a `now()`
callback wired to `play_t` so engine math stays deterministic.

## New primitives (`effects/`)

| Primitive | Input used | Notes |
|---|---|---|
| `ripple` | events matching `layer.source` (`"audio"`, `"key"`, `""` = all) | Each event expands a ring: radius `speed*age`, amplitude `strength*exp(-density*age)`, gaussian band of half-width `scale`. Palette sampled by normalized age (bright front -> dim tail). |
| `screenfield` | `screen_cells` | Planar projection: `origin` = screen center, `scale` = screen width (m), height = width*9/16. Emitter (x,y) -> bilinear cell sample, edges clamp — desk-height emitters pick up the nearest screen edge row. |
| `level` | `audio_level` | Coverage = level * opacity; palette sampled at the level value — a loudness-following wash. |

`EffectLayer.source` filters ripple events; layers without a source
consume every event. Layer fields are not persisted — presets rebuild
them, so no scene-JSON change was needed.

## Providers (`inputs/` — host side)

Qt-free, unit-tested: `InputBus`, `OnsetDetect` (adaptive
short/long-EMA onset detector with refractory + sensitivity — onset,
not perfect beat tracking), `KeyMap` (`"Key: Q"` LED-name -> VK
table; unmapped names return -1).

Host-side only:

- `AudioLoopback` — WASAPI shared-mode loopback on the default
  render endpoint (`eRender`/`eConsole`,
  `AUDCLNT_STREAMFLAGS_LOOPBACK`), own thread, CoInit per-thread.
  float32/pcm16 handled; `AUDCLNT_BUFFERFLAGS_SILENT` feeds zero
  energy (silence -> level decays -> base layer returns). Default-
  device changes via `IMMNotificationClient` and capture errors via
  `AUDCLNT_E_DEVICE_INVALIDATED` both trigger reinit. Only energies
  cross the API boundary — no audio buffers are kept.
- `KeyHook` — `WH_KEYBOARD_LL` on a dedicated thread with its own
  message loop (required for LL hooks); tracks down-state to suppress
  auto-repeat; pushes `{t, "key", vk}`; `Stop` posts an unhook+quit
  message so the hook uninstalls on its own thread. Only VK codes
  pass the boundary, translated to positions and discarded — no text
  history is retained.
- `ScreenSampler` — `QObject` + `QTimer` (~10 Hz) on the UI thread;
  `QScreen::grabWindow(0)` on the selected display -> 12x6 smooth
  scale -> temporal lerp + luminance cap -> `SetScreenGrid`. A failed
  grab reports "capture unavailable" and leaves the grid empty (the
  `screenfield` layer then contributes nothing).

## Key-position translation

The keyboard object's emitters carry zone-LED `address`es; the bridge
asks the adapter for `GetLEDName(zone_start + address)` and matches
each `"Key: ..."` name through `KeyMap` to a VK. The result is a
`vk -> emitter index` table rebuilt on refresh — the actual hardware
layout, not a guessed grid. Unmapped VKs (or a missing keyboard)
fall back to `has_pos=false`, so the ripple still spawns at the
preset's origin.

## Bridge + presets

`SceneBridge` owns the bus and providers; `tick()` snapshots the bus,
resolves key positions, and calls `Evaluate(doc, play_t, frame, &in)`.
New slots: `setAudioInput/setKeyInput/setScreenInput/setScreenIndex/
setAudioSensitivityPct/setRippleDecayPct`; decay scales `density` on
`ripple` layers at rebuild. Input flags persist beside `"scene"` in
the plugin settings (`"inputs"` key) so a saved scene restores its
sources; reactive presets auto-enable their source on selection (the
checkbox reflects it and can still be turned off).

New presets (registry grows 6 -> 9, `PresetInfo.needs` marks the
required source):

| Preset | Layers | Source |
|---|---|---|
| Bass shockwave | dark base + `ripple` (origin = case interior) + `level` wash | `audio` |
| Key ripple | dark base + `ripple` at mapped key positions | `key` |
| Screen atmosphere | low base + `screenfield` (all emitters) | `screen` |

## UI

New compact "Inputs" row: `Audio` + sensitivity slider, `Keys`,
`Screen` + display combo, `Decay` slider (ripple lifetime). Sources
report their state through `statusMessage` when they fail.

## Tests

`studio_scene_test.cpp` additions (all deterministic): ripple ring
position/decay/source-filter/origin-fallback, screenfield cell
sampling + clamping + empty grid, level coverage, InputBus
bounds/pruning/grid, OnsetDetect silence/spike/refractory/
adaptation, KeyMap lookups, new-preset evaluation with a synthetic
`InputState`. `build-studio-tests.bat` compiles the three Qt-free
inputs sources.

## Verification status

Verified this session (2026-09-15):

- `tests/build-studio-tests.bat` — **178 checks, 0 failures** (was
  105; +73 covering InputBus bounds/pruning/grid, OnsetDetect
  silence/spike/refractory/adaptation/sensitivity contrast, KeyMap
  name lookups, ripple front position/propagation/source filter/
  origin fallback/decay/future-event guard/null input/determinism,
  screenfield cell sampling/blend/empty+malformed grid, level
  coverage, all three reactive presets under a synthetic InputState,
  and same-input determinism).
- `plugins/DesktopLightingStudio/build-plugin.bat` — clean build,
  deployed via xcopy; deployed DLL passes the `LoadLibraryExW` +
  `ALTERED_SEARCH_PATH` probe (SAC verdict OK, deps resolve).
- Host smoke (2026-09-15): plugin loads, Studio tab shows the Inputs
  row (Audio/Sens, Keys, Screen/display, Decay) and the three
  reactive preset cards; toggles respond. (First deploy attempt was
  silently blocked by a running host holding the DLL — deploy now
  fails loudly, `deploy-only.bat` covers the close→redeploy loop.)

Build fixes needed along the way: `IMMDeviceEnumerator` exposes
`Register/UnregisterEndpointNotificationCallback` (not
`NotificationClient`), and the .pro needed `-luser32` for the
hook/message-loop symbols.

Pending (needs the running elevated host + eyes on hardware):

- Audio onset rings visible on the desk while sound plays; silence
  decays back to the base layer.
- Key presses ripple at the correct physical key (G512 LED names
  resolve through `KeyMap`); unmapped keys fall back to the preset
  origin.
- Screen atmosphere follows a test image on the chosen display;
  "capture unavailable" leaves the base layer.
- Input toggles disconnect cleanly (threads join on disable/close);
  burst typing does not grow the event queue.

## Explicitly deferred

- Timeline editor (gate does not require it).
- DXGI desktop duplication — `grabWindow` misses DRM/protected
  content; acceptable for ambient color with the documented fallback.
- Per-source event routing to sub-selections beyond `targets`.
