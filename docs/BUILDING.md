# Building OpenRGB on Windows (baseline)

Pinned configuration — matches upstream CI for the current 64-bit Windows build
(`scripts\build-windows.bat 6.8.3 2022 64`).

| Component | Version | Install location |
|---|---|---|
| OpenRGB source | `release_1.0` — commit `81bbe18a` (tagged `release_1.0`) | `OpenRGB/` submodule |
| Visual Studio Build Tools | 2022 17.14.40, `Microsoft.VisualStudio.Workload.VCTools --includeRecommended` | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| Qt | 6.8.3, arch `win64_msvc2022_64` (via `aqtinstall`) | `C:\Qt\6.8.3\msvc2022_64` |
| jom | 1.1.7 (`https://download.qt.io/official_releases/jom/jom.zip`) | `C:\Qt\jom` |
| Python (for aqtinstall) | 3.12.10 user-scope | `%LOCALAPPDATA%\Programs\Python\Python312` |
| PawnIO kernel driver | already installed (service `PawnIO`, running) | `DriverStore\...\PawnIO.sys` |

## Toolchain setup (one-time)

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Python.Python.3.12 --scope user
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
# jom: unzip https://download.qt.io/official_releases/jom/jom.zip to C:\Qt\jom
```

## Build

```bat
cd OpenRGB
scripts\build-windows.bat 6.8.3 2022 64
```

Produces `OpenRGB Windows 64-bit\OpenRGB.exe` with Qt DLLs deployed by
`windeployqt`. `Qt6Core5Compat.dll` and `Qt6OpenGL.dll` are copied manually
(needed by the Effects plugin).

## Runtime requirements

- **PawnIO driver** must be installed for SMBus (DRAM RGB) and Super-I/O
  access, and OpenRGB must run **elevated** (or as a service) to open it.
  Without elevation only HID/NVAPI devices are detected. The required
  `.bin` PawnIO modules ship in `dependencies/PawnIO/modules/` and must sit
  next to the exe.
- `libusb-1.0.dll` / `hidapi.dll` are deployed by the build.

## Notes

- Upstream CI also has a legacy Qt5 target (`5.15.0 2019 64`, MSVC 2019).
  We use the current primary target (Qt 6.8.3 + MSVC 2022).
- The previously installed `C:\Program Files\OpenRGB` build is
  `0.9.2026` (reports itself `v0.9+ (1.0rc3.1)`, commit `5e81e26`) — a Qt5
  pipeline build. Our pinned baseline is the `release_1.0` tag.
