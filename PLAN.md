# Plan: one RGB app for your Windows PC

**Chosen approach:** extend OpenRGB with native Lian Li wireless support and close verified gaps for your other devices.

**Scope:** RGB only. FanControl, fan curves, pump speeds, peripheral buttons/macros, and GPU tuning remain outside this project.

**End state:** one OpenRGB interface for your lighting, with profiles, synchronized effects, and reliable startup/resume behavior. No L-Connect dependency for RGB once the replacement passes validation.

## 1. Hardware coverage

Start with an explicit inventory — not assumptions based on brand-level support.

| Entity | Intended control | Verification needed |
|---|---|---|
| X870E AORUS ELITE WIFI7 ICE | Onboard lighting and available ARGB headers | Exact controller support and header mapping |
| `ARGB_V2_1`: four Gigabyte fans | Color, brightness, effects | LED count; whether fans mirror or form an addressable chain |
| `ARGB_V2_2`: CPU cooler | Cooler lighting | Physical connections and independently controllable regions |
| `ARGB_V2_3`: top-GPU fan | Fan lighting | LED count and ordering |
| Three Lian Li SL Wireless fans | Group, per-fan, and validated per-LED control | Native driver, physical mapping, reliable update rate |
| Two Corsair Vengeance RGB DIMMs | Module/LED lighting | Exact hardware revision and access compatibility |
| AORUS RTX 5080 MASTER ICE | Available RGB regions | Exact PCI subsystem ID and supported lighting commands |
| Razer, Logitech, SteelSeries peripherals | Available lighting zones | Exact models, connection modes, and lighting capabilities |

The header assignments are documented in `Cooling and RGB.md` (Obsidian vault, `Technology/`). They still require physical confirmation.

**Important:** devices on a mirrored ARGB splitter cannot become independently addressable through software. The interface must represent that limitation honestly.

## 2. Architecture

Use OpenRGB's existing application and controller architecture rather than build a second application around it.

| Component | Responsibility |
|---|---|
| Existing OpenRGB interface | Device selection, colors, zones, profiles, tray operation |
| Existing device drivers | Motherboard, RAM, GPU, and supported peripherals |
| New native Lian Li controller | Discovery, wireless transport, RGB uploads, acknowledgments, recovery |
| Compatible Effects plugin | Cross-device animation using supported Direct modes |
| Existing lifecycle facilities | Startup, suspend/resume, configuration persistence |

**Implementation direction:** C++17 and Qt, following the selected OpenRGB revision. Prefer its existing libusb integration with the installed WinUSB driver rather than introducing a separate Windows USB stack without need.

For Lian Li, separate four responsibilities:

- **Protocol:** pure packet construction, parsing, validation, and compression.
- **Transport:** USB ownership, bounded reads/writes, disconnect handling.
- **Runtime:** discovery, fresh device state, upload scheduling, keep-alive, retries.
- **OpenRGB adapter:** exposes device identity, LEDs, zones, colors, and supported modes.

One runtime owns each TX/RX pair. Fan-group adapters share that runtime; they do not independently open the same dongles.

## 3. Implementation milestones

### Milestone 0 — establish coverage and a reproducible baseline

1. Select and pin an OpenRGB revision and matching Windows toolchain.
2. Build it unchanged before adding features.
3. Inventory exact device IDs, firmware versions, zones, and current RGB owners.
4. Test existing support one hardware group at a time.
5. Back up relevant lighting profiles before controlled testing.
6. Confirm how FanControl reaches the fans and whether stopping L-Connect affects cooling behavior.

**Exit gate:** a coverage matrix marking every entity as verified, partially supported, or blocked, plus a working baseline build.

Do not add guessed device IDs or enable experimental bus access simply to make detection succeed.

### Milestone 1 — turn the smoke-test knowledge into trustworthy protocol tests

The current harness reports one successful sequence, but it is not a production specification. Areas that require regression coverage before porting: inconsistent `$script.latest` versus `$script:latest` references, broad exception suppression, and a display hold that can finish without confirming recovery from drift (see `Worker.ps1` lines 97-139 in the smoke-test workspace).

Build offline tests for:

- RGB headers, chunk boundaries, addressing, and LED layouts.
- Complete, empty, truncated, and malformed discovery replies.
- Temporary absence versus actual USB failure.
- Stale state and foreign-master observations.
- Retry exhaustion and cancellation.
- Acknowledgment followed by effect loss.
- Restoration failure remaining a failure.

Replace the `yuz.dll` dependency with a license-verified, compatible open-source codec. Validate it against known payloads and independent decode results — not round trips alone.

**Exit gate:** deterministic tests pass without hardware or vendor DLLs.

### Milestone 2 — implement the native wireless runtime

Implement:

- Conservative detection of the known TX/RX pair and supported fan variant.
- Validation of existing binding; **no automatic pairing or channel reassignment**.
- Discovery with timestamped observations and bounded handling of missing records.
- Revalidation of identity and addressing before writes.
- Serialized RGB uploads and keep-alive traffic.
- Bounded retries that distinguish protocol failures from transport failures.
- Clean cancellation, teardown, and reconnect behavior.
- Diagnostics for last successful observation, upload, retry, and failure.

Maintain separate **desired**, **observed**, and **confirmed** lighting states. Receiving a USB write success is not equivalent to the LEDs displaying the requested effect.

**Exit gate:** repeated static-color and restore tests succeed, including missing replies and disconnects, without changing cooling settings.

### Milestone 3 — expose the fans properly in OpenRGB

Add the native detector and `RGBController` adapter.

- Stable device identity based on controller/group identity, not discovery order.
- Human-readable group and fan names.
- Physically validated LED ordering and orientation.
- Whole-group, per-fan, and supported per-LED color changes.
- Brightness through native support or consistent software scaling.
- Save/load through OpenRGB profiles.
- Explicit unavailable/error states rather than apparent success.

Initially support the tested SL Wireless hardware, not every wireless Lian Li product.

**Exit gate:** a saved profile restores distinct colors to the correct fans after an application restart.

### Milestone 4 — complete coverage of the remaining entities

For each motherboard, GPU, RAM, or peripheral gap:

1. Determine whether it is configuration, permissions, identification, or a genuinely missing protocol.
2. Fix only the confirmed gap using the existing driver family where appropriate.
3. Test every exposed zone physically.
4. Check that unrelated functions remain intact.
5. Record unsupported capabilities instead of inventing zones.

Disable competing RGB ownership selectively and reversibly. A peripheral's vendor application may still be needed for non-RGB features.

**Exit gate:** every inventory entry has a verified outcome. Any unsupported entity remains an explicit release blocker or requires your approval for reduced scope.

### Milestone 5 — synchronized effects and profiles

Reuse the OpenRGB Effects plugin, which supports synchronization across Direct-mode devices.

Start with:

- Static scenes.
- Breathing.
- Color cycling.
- Slow waves/rainbows.
- Named profiles for daily use and lights-off.

For wireless output:

- Measure reliable update throughput and latency.
- Keep only the newest pending frame when updates arrive faster than transmission.
- Finish or safely cancel transactions without interleaving packet chunks.
- Prioritize connection maintenance over animation traffic.
- Rate-limit to measured capabilities.

**Exit gate:** cross-device effects operate without growing queues, starving keep-alives, or freezing the interface.

High-rate audio/screen effects are a later capability test. If wireless throughput is insufficient, report the limitation; preloaded animations are not automatically equivalent to synchronized streaming.

### Milestone 6 — startup, resume, failure recovery, and packaging

- Restore the chosen profile at login.
- Reacquire devices and reapply desired lighting after resume or reconnect.
- Prevent duplicate application instances from competing for controllers.
- Provide configurable exit behavior: leave lighting or request lights-off.
- Preserve settings through upgrades.
- Ship a versioned Windows package with matching plugin versions and third-party notices.
- Keep a reversible path back to the previous lighting setup.

**Exit gate:** startup and recovery work without manual vendor-app intervention.

Lights staying off after shutdown depends on device firmware and standby power. That must be tested per device; an application cannot guarantee control after Windows has stopped.

## 4. Release acceptance

Proposed release tests — not completion-time estimates:

- **30 consecutive** Lian Li color/profile cycles.
- **10 sleep/resume cycles** without manual recovery.
- **5 Windows restarts** with correct profile restoration.
- **8-hour mixed static/effect soak** without unbounded memory or queue growth.
- Peripheral disconnect/reconnect tests.
- Missing-device, failed-write, and application-crash recovery tests.
- Physical confirmation of every named lighting zone.
- No fan/pump-control commands from the new driver and no observed cooling regression.

Passing the Lian Li test alone does **not** mean the whole-machine application is complete.

## 5. Delivery and maintenance

Deliver in independently usable stages:

1. **Verified baseline:** existing OpenRGB coverage and a complete gap list.
2. **Unified static RGB:** all validated devices and the wireless fans in one interface.
3. **Unified effects:** synchronized scenes within measured hardware limits.
4. **Daily-use release:** startup/resume recovery, packaging, and regression tests.

Keep the fork small and submit coherent driver fixes upstream. Upstream acceptance is a maintenance goal, not a prerequisite for using your build.

## 6. Scope exclusions

Explicitly out of scope:

- Fan speed control, PWM commands, fan curves.
- Pump speed, thermal sensors, thermal automation.
- A FanControl plugin.
- Linux support.
- A separate cooling daemon.
- Unverified physical zones presented as independently controllable.
- Startup controller behavior until the production controller has proven recovery behavior.

## 7. References

- Smoke-test workspace: `C:\Users\daniel\Documents\Codex\2026-09-12\why\outputs\lian-li-smoke\`
- Wireless protocol reference: `https://github.com/sgtaziz/lian-li-linux`
- OpenRGB source: `https://github.com/CalcProgrammer1/OpenRGB`
- OpenRGB RGBController API: `RGBController/RGBController.h`
- OpenRGB SDK: `https://openrgb.org/sdk.html`
- OpenRGB plugins: `https://openrgb.org/plugins.html`
- Vault notes: `Technology/Cooling and RGB.md`, `Technology/Lian Li SL Wireless smoke test.md`
