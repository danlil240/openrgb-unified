# Hardware inventory — Milestone 0

Collected 2026-09-14. Source: live PnP/WMI enumeration + OpenRGB `release_1.0`
device tables + installed-OpenRGB detection logs.

## System

- Motherboard: **Gigabyte X870E AORUS ELITE WIFI7 ICE**, BIOS F10 (2026-05-21)
- CPU: AMD AM5 (iGPU `1002:13C0`, subsystem `1458:D000`)
- GPU: **Gigabyte AORUS RTX 5080 MASTER ICE** —
  `PCI\VEN_10DE&DEV_2C02&SUBSYS_418C1458` (GB203/Blackwell)
- RAM: 2× Corsair **CMH64GX5M2B6000Z30** (Vengeance RGB DDR5, 32 GB each,
  6000 MT/s) — SMBus-controlled RGB

## USB devices relevant to RGB

| Device | ID | Notes |
|---|---|---|
| Lian Li SL Wireless TX | `USB\VID_0416&PID_8040` (`SLV3TX_V1.6`) | transmitter dongle |
| Lian Li SL Wireless RX | `USB\VID_0416&PID_8041` (`SLV3RX_V1.6`) | receiver dongle |
| Motherboard ARGB MCU | `USB\VID_048D&PID_5711` | ITE IT5711-class RGB Fusion 2 controller |
| Razer Basilisk V3 Pro | `USB\VID_1532&PID_00AA` (+`RZVIRTUAL` HyperPolling dongle) | wired PID detected |
| Logitech G512 | `USB\VID_046D&PID_C33C` | keyboard, HID++ 2.0 |
| Logitech USB Receiver | `USB\VID_046D&PID_C548` | Lightspeed receiver; paired mouse model **TBD** |
| SteelSeries GameDAC | `USB\VID_1038&PID_1280` + `PID_1282` | Arctis Nova Pro base station; RGB zones unconfirmed (likely none — OLED screen only) |

## Source-level support in `release_1.0`

| Entity | Status in source |
|---|---|
| X870E AORUS ELITE WIFI7 ICE | Named layout `x870e_aor_elite_wifi7_ice_5711_device`; detector `048D:5711` registered |
| ARGB_V2_1/2/3 headers | Exposed via IT5711 zone table; **resizable zones (ship at 0 LEDs — must resize via `sizes.ors`/SDK)**; physical mapping **confirmed live** (see below) |
| AORUS RTX 5080 MASTER ICE | `GigabyteRGBFusion2BlackwellGPU` (I2C 0x75, `AORUS_MASTER_5080_LAYOUT`) — 6 HW zones: 0–2 fans (8 LED each), 3 side logo, 4+5 top logo |
| Corsair Vengeance RGB DDR5 | `CorsairDRAMController` — CMH = 10 LEDs/DIMM; requires SMBus (PawnIO + elevation) |
| 3× Lian Li SL Wireless | **No support** — `0416:8040/8041` absent from detectors; this is the new driver work |
| Razer Basilisk V3 Pro | `RAZER_BASILISK_V3_PRO_WIRED_PID 0x00AA` supported |
| Logitech G512 | `0xC33C` in HID++ 2.0 table with top-strip zone |
| Logitech mouse (C548 receiver) | Receiver enumerates (1 paired device, WPID `B042`); no RGB controller registered — model unsupported/TBD |
| SteelSeries GameDAC | No `1280/1282` entries; no RGB zones (OLED screen only) |

## Verified zone→physical mapping (2026-09-14, user-observed)

| Zone | Physical | Verified |
|---|---|---|
| ARGB_V2_1 | 4× Gigabyte case fans (shared strip — mirrored splitter, single logical zone) | ✔ red write seen |
| ARGB_V2_2 | CPU cooler pump + 3 fans | ✔ green write seen |
| ARGB_V2_3 | Single top-GPU fan | ✔ blue write seen |
| LED_C | Onboard LED | write accepted; not visually confirmed (possibly hidden under GPU) |
| Chipset Accent | Onboard chipset LED | write accepted; not visually confirmed |
| GPU Right/Left/Middle Fan | Fan rings, 8 LED each | ✗ no light at 0% RPM — **suspected zero-RPM-linked lighting; retest under load** |
| GPU Side Logo | Side logo | ✔ yellow write seen |
| GPU Top Logo | — | no physical top-logo lighting on this card |
| Basilisk: Scroll Wheel / Logo / LED Strip | mouse zones | ✔ all seen after stopping Synapse (Synapse re-asserts control while running) |
| G512 Keyboard | 107 per-key LEDs | ✔ blue write seen |
| G512 Extras | — | write accepted; no visible extras LED on this unit |
| Corsair DIMM 0/1 | 10 LEDs each | detected post-reboot; color write pending visual confirm |

## RGB software ownership — observed contention

| App | Contention observed |
|---|---|
| L-Connect 3 | Service owns `0416:8040/8041` dongles; its OpenRGB integration **spawns elevated OpenRGB processes** (explained six stale instances). Must be stopped for isolated testing. |
| Razer Synapse 4 | **Continuously re-asserts Basilisk effect** — our writes were overridden until `RazerAppEngine`/`razer_elevation_service` were stopped |
| GIGABYTE Control Center | `GigabyteUpdateService`/`AorusLcdService` run at boot; stopped during tests (GPU fan rings still dark — not proven to be contention) |
| Logitech Options+ | `logi_lamparray_service` registers a Windows Dynamic Lighting lamp array — potential G512 re-assertion |
| SteelSeries GG | Engine/Prism processes run at boot; no RGB target found anyway |
| FanControl | Fan speed only (`/lpc/it8696e/*`) — **no RGB conflict**; also shares PawnIO |
| Windows Dynamic Lighting | Lamp-array provider present; watch for re-assertion on G512 |

`scripts/post-reboot-check.ps1` stops all of the above (except FanControl)
in one elevated pass before starting the test server.

## Cooling path (Milestone 0 step 6 — confirmed)

FanControl reaches **all** fans via the motherboard ITE IT8696E EC and GPU fans
via NVAPI. The "Lian li side intake" group is `/lpc/it8696e/control/2` —
the wireless fans' PWM comes from the motherboard header through the receiver's
PWM pass-through, **not** through L-Connect software. Stopping L-Connect only
affects RGB; cooling is unaffected. No FanControl Lian Li plugin is installed
(`Plugins/` empty).

## Backups (2026-09-14)

- `%APPDATA%\OpenRGB\OpenRGB.json`, `sizes.ors` → `backups/2026-09-14/`
- `C:\ProgramData\Lian-Li\L-Connect 3\{settings,profile,slv3,device}` →
  `backups/2026-09-14/lconnect3/` (26 files)
