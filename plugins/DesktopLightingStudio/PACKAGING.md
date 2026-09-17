# Desktop Lighting Studio — packaging manifest

What a release of this plugin must contain, and how to verify it.
`verify-package.bat` checks the file-side of this manifest against
`out/` and fails loudly on anything missing.

## Producing the payload

`build-plugin.bat` (build + deploy) and `build-only.bat` (build, no
deploy) produce `out/` via qmake/jom; `build-plugin.bat` then runs

```
windeployqt --no-patchqt --no-translations --no-system-d3d-compiler \
            --no-compiler-runtime --qmldir ui out\DesktopLightingStudio.dll
```

and copies `out/*` into the host's `plugins\DesktopLightingStudio\`
folder, the loose `ui/` tree beside it, and the shared `Qt6*.dll` /
`dxcompiler.dll` / `dxil.dll` / `opengl32sw.dll` next to OpenRGB.exe
(Windows does not search the plugin's own folder for DLL deps).

`build-only.bat` never runs windeployqt or any copy — the routine
validation path is deploy-free and must stay that way.

## Payload contents

### 1. The plugin

| File | Notes |
| --- | --- |
| `DesktopLightingStudio.dll` | The plugin itself. `DesktopLightingStudio.exp`/`.lib` are link artifacts, not required at runtime. |

### 2. Qt runtime DLLs (windeployqt output, flat in `out/`)

Core/gui: `Qt6Core`, `Qt6Gui`, `Qt6Widgets`, `Qt6Network`, `Qt6OpenGL`,
`Qt6Svg`.
QML/Quick: `Qt6Qml`, `Qt6QmlMeta`, `Qt6QmlModels`,
`Qt6QmlWorkerScript`, `Qt6Quick`, `Qt6QuickLayouts`, `Qt6QuickShapes`,
`Qt6QuickEffects`, `Qt6QuickTemplates2`, `Qt6QuickControls2`,
`Qt6QuickControls2Impl`, `Qt6QuickWidgets`, `Qt6ShaderTools`.
Quick3D: `Qt6Quick3D`, `Qt6Quick3DUtils`, `Qt6Quick3DRuntimeRender`,
`Qt6Quick3DHelpers`, `Qt6Quick3DHelpersImpl`.
Controls styles (windeployqt pulls all style plugins):
`Qt6QuickControls2Basic`, `…BasicStyleImpl`, `…Fusion`,
`…FusionStyleImpl`, `…Imagine`, `…ImagineStyleImpl`, `…Material`,
`…MaterialStyleImpl`, `…Universal`, `…UniversalStyleImpl`,
`…WindowsStyleImpl`, `…FluentWinUI3StyleImpl`.
Software rasterizer + D3D shader compiler: `opengl32sw.dll`,
`dxcompiler.dll`, `dxil.dll`.

### 3. Qt plugins (subdirs of `out/`)

`platforms/qwindows.dll`, `styles/qmodernwindowsstyle.dll`,
`imageformats/{qgif,qico,qjpeg,qsvg}.dll`, `iconengines/qsvgicon.dll`,
`tls/{qcertonlybackend,qschannelbackend}.dll`,
`networkinformation/qnetworklistmanager.dll`,
`generic/qtuiotouchplugin.dll`.

### 4. QML modules (`out/qml/…`) — gathered from `--qmldir ui`

Required import surface of `ui/` (see `grep "^import" ui/**/*.qml`):
`QtQuick`, `QtQuick.Controls.Basic`, `QtQuick3D`,
**`QtQuick3D.Helpers`** — plus transitives.

Directories that must exist after windeployqt:
`qml/QtQml`, `qml/QtQml/Models`, `qml/QtQml/WorkerScript`,
`qml/QtQml/XmlListModel`, `qml/QtQuick/Controls` (+ `Basic`,
`Fusion`, `Imagine`, `Material`, `Universal`, `Windows`,
`FluentWinUI3`, `impl`), `qml/QtQuick/Dialogs`, `qml/QtQuick/Effects`,
`qml/QtQuick/Layouts`, `qml/QtQuick/Shapes`, `qml/QtQuick/Templates`,
`qml/QtQuick/Window`, `qml/QtQuick3D`, **`qml/QtQuick3D/Helpers`**
(the module the scene's `ExtendedSceneEnvironment`/helpers need —
regressions here historically shipped a black viewport),
`qml/QtQuick3D/AssetUtils`, `qml/QtQuick3D/Effects`,
`qml/QtQuick3D/Particles3D`.

### 5. Resources baked into the DLL (`ui/studio.qrc`)

Not file-verifiable — verified by loading `:/studio/...` paths (the
`studio_config_store_test` suite's fresh-install test does exactly
this against a temp workspace):

- `:/studio/{studio,device,effect}.schema.json` — copied into
  `<workspace>/schemas/` by `ConfigStore::EnsureWorkspaceDir`.
- `:/studio/presets/devices/*.device.json` — 13 bundled device
  types, materialized into `<workspace>/presets/devices/`.
- `:/studio/presets/effects/*.effect.json` — 9 bundled looks,
  materialized into `<workspace>/presets/effects/`.
- `:/studio/*.qml` — the whole `ui/` tree (see studio.qrc).

Manual check when the qrc changes: load the plugin, confirm the
workspace dir gains all three schemas + both preset sets, and the
scene renders (no missing-module errors in the diagnostics log).

### 6. Loose `ui/` override directory

`build-plugin.bat` copies `ui/` next to the deployed plugin DLL so
QML can iterate without a rebuild (file-layer QML overrides the qrc
contents — see `SceneBridge`'s source order). A release intended for
a fixed QML set may omit it; the dev deploy keeps it.

### 7. Licenses

Plugin sources carry `SPDX-License-Identifier: GPL-2.0-or-later`
headers; the host repo ships `OpenRGB/LICENSE` (GPL-2.0). Qt runtime
components are LGPL — the windeployqt-gathered DLLs/QML modules are
covered by the host's existing Qt notices; no extra notice file is
produced by this plugin today. (If a release bundles third-party
assets under `presets/` or `ui/` later, their notices must be added
here.)

## Verification

1. `plugins\DesktopLightingStudio\verify-package.bat` — expected vs
   present files under `out/`; exits non-zero on any miss.
2. `tests\studio_config_store_test` — fresh-install materialization
   loads `:/studio` resources end to end.
3. Manual (acceptance doc): deploy into a clean host profile and
   confirm the plugin loads, schemas + presets materialize, and the
   scene renders.
