# Milestone 0 report — OpenRGB baseline

Date: 2026-09-14. Branch: `milestone-0-baseline`.
Baseline: OpenRGB `release_1.0` @ `81bbe18`, built unchanged with MSVC 2022 +
Qt 6.8.3 (matching upstream CI recipe, `scripts/build-windows.bat 6.8.3 2022 64`).

## Baseline build

- Builds clean; output `OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe` (~10.6 MB).
- Runs as SDK server with `--localconfig --noautoconnect --server --verbose`.
- Elevation required for SMBus (DIMMs) and most motherboard access.

## Device coverage matrix (live-verified)

| Hardware | Detected | Controlled | Verified zones |
|---|---|---|---|
| X870E AORUS ELITE WIFI7 ICE | ✔ | ✔ | ARGB_V2_1→4 case fans, ARGB_V2_2→cooler pump+3 fans, ARGB_V2_3→top-GPU fan. LED_C/Chipset Accent write OK, not visually located |
| Corsair Vengeance RGB DDR5 ×2 | ✔ (post-reboot) | ✔ writes accepted | 10 LEDs/DIMM |
| RTX 5080 MASTER ICE | ✔ | partial | Side Logo ✔; Top Logo n/a on this card; **fan rings dark at 0% RPM — retest under load** |
| Razer Basilisk V3 Pro | ✔ | ✔ | wheel/logo/strip — only while Synapse stopped |
| Logitech G512 | ✔ | ✔ | 107 per-key LEDs; Extras zone has no visible LED |
| Lian Li SL Wireless ×3 | ✗ | — | **No driver — project scope** (`0416:8040/8041`) |
| Logitech mouse (C548) | ✗ | — | receiver seen, no controller registered |
| SteelSeries GameDAC | ✗ | — | no RGB zones on hardware |

## Findings

1. **PawnIO device-node failure blocked all SMBus users** — OpenRGB *and*
   FanControl lost `/lpc/it8696e/*` sensors. Device restart
   (`scripts/fix-pawnio.ps1`) restored both; no reboot needed for the driver.
2. **Corsair DIMMs need a clean boot state.** Once the SMBus host/DIMM SPD
   channel wedges, modules stop answering until reboot — detection then works
   reliably (2× controllers, 10 LEDs each).
3. **Competing owners are the norm, not the exception.** Synapse re-asserts the
   mouse every frame; L-Connect's OpenRGB integration spawns elevated OpenRGB
   instances. `scripts/post-reboot-check.ps1` isolates them for testing.
4. **ARGB_V2_* zones ship at 0 LEDs** — resizable; must set size to expose the
   strip (resized to 32 for testing; physical strips mirror whatever count
   they actually have).
5. **GPU fan rings dark at idle** — all writes accepted incl. per-LED
   gradients; most likely zero-RPM-linked on the AORUS MASTER. Confirm under
   fan load before classifying as a driver gap.
6. **Lian Li wireless is the only true support gap** — everything else with
   RGB-capable hardware is covered by `release_1.0`.

## Gap list → Milestone 1+

- **New driver:** Lian Li SL Wireless TX/RX (`0416:8040/8041`) — protocol from
  `lian-li-smoke` harness: RF-sighting discovery, fresh effect IDs, keep-alive,
  bounded newest-frame-wins queues, master-change debounce.
- **Investigate:** GPU fan-ring output under load; LED_C/Chipset Accent
  physical location; Logitech C548 receiver mouse RGB.
- **Out of scope (unchanged):** fan/pump speed, FanControl plugin, L-Connect
  config writes, SteelSeries (no RGB), startup ordering.
