# Stage 2 — spatial engine and first six presets

Date: 2026-09-15. Branch: `desktop-lighting-studio`.
Parent plan: `docs/2026-09-15-desktop-lighting-studio-plan.md`.
Stage 1 record: `docs/2026-09-15-stage1-scene.md`.

## Scope for this stage

From the parent plan's Stage 2 gate:

- Deterministic wave, radial pulse, path/comet, gradient, and noise
  primitives.
- Layers, masks, blend modes, world/local coordinates, shared clock,
  output pacing.
- The six nonreactive presets: Aurora, Reactor, Comet, Liquid chrome,
  Portal, Embers.
- Scene cards, reproducible Remix, saved startup scene selection.

Gate: a wave visibly crosses the keyboard, mouse, and confirmed PC
lights in spatial order; preview and output colors match before device
calibration; a slow output does not freeze the editor or build a frame
backlog.

## Effects engine (`effects/` — Qt-free, deterministic)

Pure functions of `(SceneDocument, t, params)` — no clock, no Qt, no
hardware. The bridge owns time; tests pass fixed `t`.

- `EffectTypes` — `ColorF` (0..1 working space), `Palette` (stop list,
  interpolated `Sample(u)`), `BlendMode` (`Replace` / `Add` / `Screen`),
  `CoordSpace` (`World` / `Local` — local is per-object, world is the
  shared desk frame), `EffectLayer`.
- `EffectLayer` — `primitive`, `space`, `blend`, `opacity`, `speed`,
  `scale` (wavelength/spacing/noise frequency, meters or per-primitive),
  `phase`, `density` (noise contrast / pulse sharpness), `origin`,
  `direction`, `path` (comet waypoints), `palette`, `targets`.
- `targets` — empty = every output-owning object; otherwise matches
  object id, `geometry`, or emitter `group`. Selections never write
  through `Linked` copies — effects evaluate on `Device` owners only,
  sampled in the shared group frame (plan mirror rule); copies display
  the owner's colors at their own transform.
- `EffectEngine::Evaluate(doc, t, frame)` — for each `Device` object's
  emitter: `world = TransformPoint(object.transform, local_pos)`,
  composite every matching layer in order onto the emitter's painted
  base color, write `frame[owner_id][index]`. Emitters/objects not
  covered by any layer fall back to painted colors automatically.
- `FrameColors` (`std::map<object_id, std::vector<SceneColor>>`) lives
  in `SceneTypes.h` so `output/` and `plugin/` share it.

### Primitives

| Primitive | Field evaluated |
|---|---|
| `static` | palette[0] |
| `gradient` | palette sample along `direction`, drifting with `t*speed` |
| `wave` | palette sweep + band at `dot(p, dir)/wavelength - t*speed` |
| `pulse` | concentric rings from `origin`, `scale` = ring spacing, `density` sharpens |
| `comet` | bright head + exponential tail along `path` waypoints, `speed` m/s, `scale` = tail length |
| `noise` | deterministic 3-D value noise -> palette sample; `direction` = wind, `density` = contrast |
| `spin` | angular sweep: local `atan2(z,x)` rotates fan rings; world space = conic sweep around `origin` |

All motion derives from `t` (seconds, monotonic accumulator owned by
the bridge) and per-layer params — fixed `t` + seed gives a fixed
frame. The noise hash is integer-based (splitmix), stable across
platforms.

## Presets (`effects/Presets.cpp`)

`BuildPreset(id, seed, speed, intensity)` returns curated
`EffectLayer` lists; `Remix` varies seed-selected palette phase,
direction angle, wavelength, and noise offset within preset bounds —
the seed is stored in the scene document so a remix is reproducible
and persists. Presets are code recipes for now; JSON recipe files
under `presets/` are a later expansion (thumbnails need assets anyway).

| Preset | Layers |
|---|---|
| Aurora | dark base + world `wave` (teal/violet, sweeping keys -> case) + sparse white `noise` highlights (Screen) |
| Reactor | dark base + `spin` on `fan_body`/`pump` + local `wave` climbing the DIMMs + `wave` across the keyboard |
| Comet | dark base + `comet` on a closed desk loop (keyboard -> mouse -> case -> DIMMs) |
| Liquid chrome | `gradient` (silver/ice palette, slow drift) + sparse `noise` highlights |
| Portal | dark base + world `pulse` rings from inside the case |
| Embers | `noise` (warm palette, upward wind) + high-threshold `noise` sparks (Screen) |

## Playback and pacing (`plugin/SceneBridge`)

- `play_timer` (~33 ms) advances `play_t` from a `QElapsedTimer` —
  pause freezes `t`, engine stays deterministic.
- Per tick: `engine.Evaluate(doc, play_t, frame)` on the UI thread
  (~300 emitters — trivial), then `emittersChanged` per touched owner
  (linked copies repaint through the owner).
- Live output uses newest-frame coalescing: at most one worker push in
  flight plus one pending flag; a busy worker just drops intermediate
  frames — slow wireless groups cannot build a backlog and the editor
  thread never blocks on hardware.
- `emittersOf` returns frame colors while playing (brightness-scaled,
  matching what `PushZone` writes — preview = output before
  calibration). Painted colors are untouched; pausing leaves the last
  frame on screen, pressing a scene card switches presets live.
- `doc.effect` (`preset`, `seed`, `speed`, `intensity`, `playing`)
  serializes in the v1 scene JSON — forward-compatible, so a saved
  scene is also the startup scene: `loadScene` restores the running
  preset.

## UI (`plugin/StudioTab`)

Bottom strip gains: preset buttons (Aurora/Reactor/Comet/Chrome/
Portal/Embers), Play/Pause toggle, Remix, Speed and Intensity sliders.
Everything else unchanged; the Stage 0 inspection bar stays.

## Tests (`tests/studio_scene_test.cpp` + `build-studio-tests.bat`)

New deterministic checks: palette sampling, blend math, wave spatial
ordering along the desk, pulse ring position vs `t`, comet head/tail,
noise determinism + range, spin ring ordering, mask matching by
id/geometry/group, mirrored copies never produce their own frame
entries, frame `FrameColors` override in `PushZone` semantics
(Qt-free portion), effect-state JSON round-trip, remix seed
reproducibility (same seed = same layers, different seed = different
params).

## Verification status (2026-09-15)

Done:

- `tests/out/studio_scene_test.exe`: **105 checks, 0 failures**
  (scene model + all new effect checks). Note: Smart App Control
  blocks fresh unsigned test binaries per-hash — relink until the
  verdict passes, same workaround as the plugin DLL.
- `build-plugin.bat`: compiles clean; deployed DLL + loose QML to
  `OpenRGB\OpenRGB 64-bit\plugins\DesktopLightingStudio\`.
- Host smoke: `DesktopLightingStudio.dll` and Qt Quick 3D modules
  loaded in the running process (verified via process modules); no
  QML errors in the OpenRGB log.

Pending (needs elevated host + eyes on hardware):

- Stage 2 gate items: wave visibly crossing keyboard/mouse/case in
  spatial order, preview-vs-output color match, slow-output
  responsiveness. The non-elevated run cannot see I2C devices
  (PawnIO permission denied), so the Lian Li fans are the
  observable set for a quick check; full gate needs the normal
  elevated launch.

## Explicitly deferred

- Preset transitions/crossfade and favorites (gate does not require).
- JSON recipe files + thumbnails under `presets/` (need assets).
- Per-device pacing/rate tables — pacing is global coalesced newest;
  per-device rates land with the measured rate table.
- Reactive inputs (Stage 3), gizmo placement (Stage 1 deferral).
