# Stage 0 — feasibility and current hardware snapshot

Date: 2026-09-15. Branch: `desktop-lighting-studio`.
Parent plan: `docs/2026-09-15-desktop-lighting-studio-plan.md`.

## Host and toolchain

| Item | Verified value |
|---|---|
| Repo revision | `desktop-lighting-studio` @ `40a8da0` (from `milestone-0-baseline`) |
| OpenRGB submodule | `lianli-wireless` @ `130b5623`, on `release_1.0` (`81bbe18a`) |
| Plugin API | `OPENRGB_PLUGIN_API_VERSION` = 5 (`OpenRGB/OpenRGBPluginInterface.h`) |
| Compiler | MSVC 2022 Build Tools 17.14.40 |
| Qt | 6.8.3 `win64_msvc2022_64` — **qtquick3d + qtshadertools added 2026-09-15** via `aqt install-qt ... -m qtquick3d qtshadertools` (headers, `Qt6Quick3D*.dll`, `qml/QtQuick3D` module present) |
| Plugin metadata | `Q_PLUGIN_METADATA` FILE with top-level `OpenRGBPluginAPIVersion`, `Id`, `Name`, `VersionStr`, `Url`, `Commit` — Qt wraps file contents under `MetaData`; PluginManager requires version == 5 exactly |
| Plugin scan dirs | `<config dir>/plugins/` (user) and `<exe dir>/plugins/` (system, recursive) — `PluginManager::ScanAndLoadPlugins` |
| Profile backup | `backups/2026-09-14/` — `OpenRGB.json`, `sizes.ors`, L-Connect 3 settings |

## Probe plugin

`plugins/DesktopLightingStudio/` — API-5 plugin skeleton per the plan's code
boundaries. `plugin/` holds the entry point and tab; `ui/` holds the QML probe
scene. Build: `plugins/DesktopLightingStudio/build-plugin.bat` (qmake + jom +
windeployqt `--qmldir ui`), deploys to
`OpenRGB\OpenRGB Windows 64-bit\plugins\DesktopLightingStudio\`.

Probe contents:

- QML `View3D` in a `QQuickWidget` inside a `LOCATION_TOP` "Studio" tab.
- OrbitCameraController + `View3D.pick()` picking with picked-object readout.
- RHI backend reported in the status bar (expected Direct3D11).
- Device bar: controller/zone combos, 4 s low-brightness zone flash for
  physical identification, per-zone `UpdateZoneLEDs()` wall-time measurement
  (15 samples → avg/max ms and implied updates/s; zones whose active mode
  lacks `MODE_FLAG_HAS_PER_LED_COLOR` are skipped with a reason).

Runtime verification pending: launch of the locally built host and tab
rendering/orbit/pick checks (see "Pending live checks").

**Fixed load failure (first attempt):** the initially deployed DLL was skipped
with `does not have an OpenRGBPluginAPIVersion field`. Root cause: an earlier
build had baked a wrongly-nested `MetaData` object, and `metadata.json` is not
tracked as a moc dependency, so incremental rebuilds kept the stale metadata.
`build-plugin.bat` now deletes `build\moc` before `jom`, and a
`tests/plugin_meta_check` probe (`QPluginLoader::metaData()` identical to
PluginManager's check) confirms the deployed DLL unwraps to API version 5.

**Fixed load failure (second attempt):** `LoadLibrary` then failed with
"The specified module could not be found" — Qt's plugin loader does not
search the plugin's own directory for dependencies, so the Quick/QML/Quick3D
runtime DLLs beside the plugin were never found. `build-plugin.bat` now also
copies `Qt6*.dll` (+`dxcompiler`/`dxil`/`opengl32sw`) next to `OpenRGB.exe`.
Note: a full `build-windows.bat` rebuild replaces `OpenRGB Windows 64-bit\`
entirely — rerun `build-plugin.bat` afterward to restore plugin deps and the
`plugins\` folder.

## Existing-plugin reuse comparison

Both plugins are **GPL-2.0-only** (compatible: our plugin is a GPL OpenRGB
plugin), pinned to the same OpenRGB `release_1.0` commit we build. Reference
clones live in `vendor/effects-plugin/` and `vendor/visual-map-plugin/` and are
git-ignored (reference only, not vendored code).

| Plugin | Structure | Reusable for Studio | Not reusable |
|---|---|---|---|
| Effects (`effects-plugin`) | `RGBEffect::StepEffect(vector<ControllerZone*>)` — per-zone stepping, `EffectManager` scheduling, `Layers`/`Mask` effects, `Audio/`, `ScreenCapturer/`, `shaders/` | `ControllerZone` zone wrapper (colors buffer + matrix map + segment awareness), `ColorUtils` helpers, audio/screen input plumbing for Stage 3, FPS/speed conventions | No shared 3D spatial field — effects restart per zone; effects are QWidget UIs, not world-space evaluators |
| Visual Map (`visual-map-plugin`) | `LedRouting` (LED cell rect → pixel-weight sampling), `VirtualController` (combines real zones into one device), `ZoneManager`, settings JSON | Virtual-controller grouping pattern, LED→canvas routing math, layout persistence conventions | 2D grid canvas only; no 3D scene, no per-emitter coordinates in meters |

Decision input: the Effects engine does not provide the spatial field the plan
requires (a desk-wide wave crossing devices continuously). Studio needs its own
`effects/` engine evaluating a shared world-space field; ControllerZone-style
zone access and the input-source plumbing are worth reusing or mirroring.

## Pending live checks (need elevated host + user eyes)

- [ ] Plugin loads, "Studio" tab appears, scene renders (RHI backend line)
- [ ] Orbit/zoom/pick work; tab switching and repeated open/close stable
- [ ] Controller/zone list matches `docs/hardware-inventory.md` on this boot
- [ ] Zone flash identifies one ARGB ring, a G512 region, and the Basilisk
- [ ] Write-latency table populated (per zone avg/max ms, incl. wireless group)
- [ ] Native-plugin vs companion decision recorded after probe results

## Notes

- UNELEVATED launch detects only HID/NVAPI devices; the full table and ARGB
  header writes need the elevated server (`deploy-openrgb.bat`, or
  `post-reboot-check.ps1` first to stop competing RGB owners).
- The wireless fans' true sustainable rate is bounded by RF convergence
  (~1.5 s resend window) — the latency probe measures `UpdateZoneLEDs` call
  time; treat its wireless number as call cost, not achievable animation rate.
