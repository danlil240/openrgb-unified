# Desktop Lighting Studio — proposed design and delivery plan

Date: 2026-09-15. Status: proposal for review; no implementation or hardware changes performed.

## Goal

Create a 3D version of Daniel's desktop where clicking, painting, and arranging effects on the keyboard, mouse, PC, fans, RAM, and other verified lights produces coordinated real-world lighting. Make impressive results possible through presets and a few expressive controls, with deeper editing available when wanted.

This is a staged product plan. Each stage gets a focused implementation checklist after its prerequisite measurements and design decisions are settled.

## Recommended approach

Build **Desktop Lighting Studio**, a native OpenRGB plugin with a new Studio tab. Keep hardware discovery and transport in the current OpenRGB fork. Build the scene model and effects engine as independent C++ modules so they can be tested without hardware and reused if the editor eventually needs a separate process.

| Approach | Benefits | Costs | Decision |
|---|---|---|---|
| New native OpenRGB plugin | One application; direct access to existing controllers; fits this project's established direction | Must match host plugin API and Qt build; graphics faults could affect the host | Recommended, subject to a small rendering/compatibility probe |
| Companion 3D editor using the OpenRGB SDK | Independent UI releases and process isolation | Additional process/lifecycle management; device matching and streaming over SDK | Fallback if native rendering or packaging proves impractical |
| Existing Effects + Visual Map plugins | Fastest route to synchronized presets and a custom grid | Does not by itself provide the requested full 3D model and proposed editing experience | Benchmark and reference; evaluate reuse before duplicating components |

The local host declares plugin API version 5. The baseline build report records MSVC 2022 and Qt 6.8.3; recheck the actual current build before choosing dependencies. Proposed UI: Qt Quick/QML with Qt Quick 3D embedded in the plugin's QWidget tab. Verify required modules, graphics compatibility, deployment, and license terms in the first probe. Pin the plugin to the tested host revision and compiler/runtime, not merely the API number.

Existing Effects supports Direct-mode synchronization, audio, screen matching, and shaders; Visual Map combines devices in a grid. Reusing source requires checking its license and integration boundaries. Do not assume either exposes a reusable 3D engine or a compatible runtime API. [Effects](https://openrgb.org/plugin_effects.html), [Visual Map](https://openrgb.org/plugin_visual_map.html).

## The experience

Default assumption: recognizable hardware models with accurate lighting positions. A photorealistic case model can be substituted without remapping LEDs. Exact case identity, desk dimensions, keyboard arrangement, mouse position, and internal component placement require confirmation during setup.

The main view contains a large orbitable 3D desk, a device tree on the left, and color/effect controls on the right. A small bottom strip holds scene cards and Play/Pause. Hide advanced controls until needed.

1. **Arrange:** place the PC, keyboard, and mouse on the desk. Rotate, resize, and snap parts; switch between desk, top, and case views. Make the case transparent or hide its side panel to reach internal lights.
2. **Map:** select a rendered part and use a low-brightness identification pattern to connect it to a real device or zone. Trace LED order around fan rings and strips; correct reversed direction and start angle.
3. **Create:** choose a preset, click its targets in 3D, choose a palette, and adjust speed, size, direction, and intensity. Drag a wave origin or path directly in the scene.
4. **Layer:** add a base color, a moving pattern, and reactive accents. Reorder layers, change opacity, and select replace/add/screen blend modes. Selections can target keys, individual addressable LEDs, rings, devices, or named groups.
5. **Play:** preview virtually, enable live output, and save a scene. Undo/redo covers mapping and editing. A prominent Stop returns to a chosen static scene or lights-off while the hardware remains connected.

The renderer offers a pleasant presentation view and a diagnostic LED view. Both use the same computed LED colors. Bloom and room reflections are visual approximations; they do not imply extra controllable LEDs or calibrated physical brightness.

## Hardware-specific model

Use `docs/hardware-inventory.md`, the current controller source, and `configs/zone-imports/` together. The baseline inventory predates the new wireless driver and must not be treated as current support status.

| Component | Representation and control plan |
|---|---|
| Logitech G512 | Model the confirmed 107-key lighting map; match the actual key layout and LED ordering |
| Razer Basilisk V3 Pro | Separate wheel, logo, and underglow geometry; validate LED ordering and current connection mode |
| Four case fans on ARGB_V2_1 | Four visible fan objects mapped to one shared 8-LED logical ring, as recorded in the current zone import |
| Cooler on ARGB_V2_2 | Three mirrored fan objects for indices 0–7; pump for indices 8–15, subject to a physical identification check |
| Top-GPU fan on ARGB_V2_3 | Separate mapped ring; read current count and verify direction |
| Three Lian Li SL Wireless fans | Inspect current driver-exposed zones/counts and validate per-fan addressing, ring geometry, sustainable update rate, and reconnect behavior |
| Two Corsair RGB DIMMs | Two 10-LED strips; confirm physical left/right identity and direction |
| AORUS RTX 5080 MASTER ICE | Include the verified side logo. Show fan rings as unverified until physically tested; do not fabricate top-logo lights |
| Motherboard accents | Add LED_C and chipset accents only after locating them physically |
| Other peripherals | Decorative geometry is allowed; expose lighting controls only for verified RGB-capable hardware |

Mirrored outputs need explicit semantics: all copies reference one logical emitter set, sampled in a shared group coordinate frame. They receive identical colors. Moving a decorative copy does not create a new output address or change sampling unpredictably. Users can choose a group anchor for spatial effects. Independent waves across those four fans would require different wiring/controller capabilities.

## Effects that justify the 3D model

Effects evaluate a shared spatial field at actual emitter positions. A desk-wide wave therefore reaches the keyboard, mouse, and case according to their positions, rather than restarting its phase on each device.

First preset collection:

- **Aurora:** flowing teal/violet ribbons sweep from keys into the PC, with subtle white highlights.
- **Reactor:** rotating fan rings, climbing RAM energy, and synchronized pulses through the keyboard.
- **Comet:** a bright head and fading tail follow a user-drawn path through the setup.
- **Liquid chrome:** slow pearlescent gradients with sparse moving highlights, suited to the white/ICE components.
- **Portal:** expanding waves originate inside the case and travel over the desk.
- **Embers:** warm noise and occasional sparks, with adjustable density and speed.

Next collection, after streaming is proven:

- **Bass shockwave:** audio onsets launch spatial rings; sensitivity and decay remain user-adjustable. Onset detection is not promised as perfect musical beat tracking.
- **Key ripple:** key presses originate light waves at mapped key positions and spread into the PC. Requires a separate Windows input-event source; OpenRGB LED control does not supply key events.
- **Screen atmosphere:** screen regions influence selected light groups with smoothing and brightness limits.

An optional **Remix** button varies palette, phase, scale, and layer settings within preset-defined bounds and saves the seed. It gives quick novelty without requiring shaders. A node editor, arbitrary shader editor, text-to-effect generation, and a timeline are later expansions; they are unnecessary for the first useful release.

## Internal design

Flow: scene + time + optional input signals → effect layers → logical emitter colors → calibration and brightness → output scheduler → OpenRGB controllers. The 3D preview consumes the same color buffer.

- **Scene model:** versioned scene documents, object transforms in meters, emitter coordinates, tags, named selections, and references to model assets. Use a declared right-handed coordinate system with Y up.
- **Device bindings:** persistent identities based on available serial/location/controller metadata, zone descriptors, and LED maps. Reconcile on discovery changes; ambiguous identical devices require an identification step. Never persist controller-list indices as identities.
- **Effects engine:** deterministic time/seed-based effects with world-space and device-local coordinate modes, masks, palette curves, compositing, and bounded input-event buffers. Elapsed time comes from a monotonic clock.
- **Output adapter:** only valid mapped addresses; inspect support before selecting Direct mode. Unsupported regions remain visible with a reason. Never resize zones as an incidental effect operation.
- **Scheduler:** UI rendering and hardware output run independently. Maintain at most one newest pending frame per output device/group and serialize writes. Preserve wireless transaction/keep-alive requirements inside the existing driver.
- **Calibration:** per-device brightness and channel gains, explicit color-space conversion, and optional measured gamma response. This improves consistency but cannot guarantee identical perceived colors between different LED hardware.
- **Persistence:** atomic saves for layout, bindings, assets, and scenes; version migration with a backup. Export/import portable scene bundles with relative asset references and license notices.
- **Ownership/lifecycle:** a Studio live-output switch and an explicit handoff from other effect writers. Investigate host facilities; do not invent an ownership-lock API. If exclusivity cannot be guaranteed, report conflicts and require conflicting writers to be stopped. Suspend writes while topology changes, retain desired scene state, and restore bindings after reconnect.

No new USB transport, fan-speed control, pump control, or thermal-control subsystem is needed. The startup duplication discovered in this task is a separate existing issue: resolve the competing startup mechanisms before daily-use acceptance. A new plugin must not launch a second OpenRGB process.

## Delivery stages and acceptance gates

### 0. Feasibility and current hardware snapshot

- Record host revision, current build toolchain, plugin API, controller list, zones, LED counts, modes, and current scene/profile backup.
- Load a minimal 3D scene inside an API-5 plugin tab. Verify orbit/picking, tab switching, repeated open/close, graphics backend, and packaged module loading.
- Identify one ring, one keyboard region, and the mouse; confirm shared mappings.
- Measure each output's sustainable rate and latency, including wireless. Begin with static colors and slow changes; increase only while output remains stable.
- Compare the existing Effects/Visual Map data structures and licenses for useful reuse.

**Gate:** demonstrable plugin rendering and physical light control, a capability/rate table, and a recorded native-plugin versus companion decision. Wireless performance is a measured constraint, not an assumed 60 FPS promise.

### 1. Accurate desk and static color control

- Build editable recognizable geometry for the keyboard, mouse, case shell, fans, pump, RAM, and verified GPU lighting.
- Implement placement, transparent case view, picking, device binding, LED-order calibration, linked-group display, and undo/redo.
- Add static color, palette painting, brightness, virtual preview, and explicit live output.
- Persist layouts and reload them with devices enumerated in a different order.

**Gate:** selecting any confirmed part changes the correct real lights; mirrored copies remain identical; unverified lights cannot receive writes; layout survives restart. This stage is independently useful.

### 2. Spatial engine and first six presets

- Add deterministic wave, radial pulse, path/comet, gradient, and noise primitives.
- Add layers, masks, blend modes, world/local coordinates, shared clock, and output pacing.
- Author the six nonreactive presets above with curated defaults and simple exposed controls.
- Add scene cards, favorites, transitions, reproducible Remix, and saved startup scene selection.

**Gate:** a wave visibly crosses the keyboard, mouse, and confirmed PC lights in spatial order; preview and output colors match before device calibration; a slow output does not freeze the editor or build a frame backlog.

### 3. Reactive input and deeper composition

- Add Windows audio loopback with silence/device-change handling; store no captured audio.
- Add optional key-trigger events with layout translation; retain only transient effect events, not typed-text history.
- Add optional screen sampling, display selection, smoothing, and fallback when capture is unavailable.
- Ship bass shockwave, key ripple, and screen atmosphere presets. Add a timeline only if layer controls prove insufficient for real scene creation.

**Gate:** sources disconnect cleanly; silence decays to the base layer; input bursts stay bounded; all scenes remain editable and useful without input sources.

### 4. Daily-use release

- Package against the verified host, including required Qt modules and licensed assets.
- Validate output ownership, one OpenRGB startup path, restored scenes, disconnect/reconnect, and sleep/resume.
- Provide export/import, config migration, rollback package, and readable diagnostics.
- Stop rendering the 3D viewport when hidden while keeping the effect engine running. Benchmark visible, hidden, and static-scene overhead on this machine.

**Gate:** 8-hour mixed-scene soak, 10 sleep/resume cycles, 5 Windows restarts, unplug/replug tests, and no growing queues or manual profile recovery. A device without hardware feedback is reported as commanded, not physically confirmed.

## Proposed code boundaries

Create a sibling plugin project under `plugins/DesktopLightingStudio/`; preserve unrelated in-progress changes in the OpenRGB submodule and existing tests.

| Directory | Responsibility |
|---|---|
| `plugin/` | API-5 entry point, tab integration, controller callbacks, host lifecycle |
| `scene/` | scene schema, transforms, logical emitters, linked groups, bindings |
| `effects/` | deterministic primitives, palettes, compositing, scene playback |
| `output/` | controller adapter, pacing, reconnect, calibration |
| `ui/` | QML desk view, selection tools, effect inspector, scene cards |
| `inputs/` | optional audio, key, and screen signal providers |
| `assets/` | models, emitter maps, material presets, attribution |
| `presets/` | versioned scene recipes and thumbnails |
| `tests/` | transform, mapping, compositor, fake-controller, persistence, lifecycle tests |

Hardware fixes remain in their existing OpenRGB controller folders and are isolated from editor work. In particular, source-level Direct support in `LianLiWirelessController` is not proof that high-rate streaming is ready.

## Verification strategy

Use deterministic numerical tests for transforms, mirrored mappings, ring reversal, phase continuity, blend output, fixed-seed effects, and scene round trips. Fake-controller tests cover reordered devices, missing devices, invalid LED indices, bounded queues, hot unplug, and stop/handoff. UI checks cover picking through the case, keyboard accessibility, scaling, and undo.

Physical acceptance uses a known moving marker and low-brightness color patterns. Log requested and delivered frame timing separately; visually confirm actual light behavior because a successful API call alone is insufficient. Evaluate smoothness at measured device rates and label slower groups clearly.

## Information needed before an exact model is finalized

- Confirm the case model: current zone imports mention C500 case fans and GAMING 360 ICE cooling; this does not establish exact shell dimensions or component placement.
- Obtain desk dimensions, component orientation, and a few setup reference views during the modeling stage, or let the user place simplified models manually.
- Confirm which mouse is part of the intended scene and the G512 key layout.
- Physically verify wireless LED mapping, GPU fan lighting, and hidden motherboard accents.

These details do not block the proposed architecture or a useful simplified first version.

## References

- Local: `docs/hardware-inventory.md`, `docs/milestone-0-report.md`, `configs/zone-imports/`, `OpenRGB/OpenRGBPluginInterface.h`, `OpenRGB/Documentation/OpenRGBSDK.md`, `OpenRGB/Controllers/LianLiWirelessController/`.
- [OpenRGB Effects](https://openrgb.org/plugin_effects.html): existing effect and input capabilities.
- [OpenRGB Visual Map](https://openrgb.org/plugin_visual_map.html): current grid-mapping approach and API-5 releases.
- [Qt 6.8 RuntimeLoader](https://doc.qt.io/qt-6.8/qml-qtquick3d-assetutils-runtimeloader.html): glTF/GLB model loading capability; actual module availability must be checked against the installed 6.8.3 toolchain.
