@echo off
:: Build + run the Studio performance harness (Qt-free, plain cl).
:: Measures effect-engine frame-eval p50/p95/max and transform-commit
:: cost on the spec fixtures (tests\fixtures\scenes\<name>\studio.json)
:: and prints machine-readable JSON lines.
::
:: SAC/WDAC note: this box hash-blocks fresh unsigned exes — when the
:: run step is denied, the exe still proves compile-clean; rerun it
:: manually (out\perf\studio_perf.exe) on an unblocked machine to
:: capture the acceptance numbers.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out\perf mkdir out\perf
set STUDIO=%~dp0..\plugins\DesktopLightingStudio
set JSON=%~dp0..\OpenRGB\dependencies\json
cl /nologo /EHsc /std:c++17 /O2 /I"%STUDIO%" /I"%JSON%" ^
   studio_perf.cpp ^
   "%STUDIO%\scene\SceneTypes.cpp" ^
   "%STUDIO%\scene\SceneGraph.cpp" ^
   "%STUDIO%\scene\EmitterLayout.cpp" ^
   "%STUDIO%\scene\SceneJson.cpp" ^
   "%STUDIO%\scene\BindingResolver.cpp" ^
   "%STUDIO%\scene\DefaultDesk.cpp" ^
   "%STUDIO%\effects\EffectTypes.cpp" ^
   "%STUDIO%\effects\EffectEngine.cpp" ^
   "%STUDIO%\effects\EffectJson.cpp" ^
   "%STUDIO%\effects\Presets.cpp" ^
   "%STUDIO%\inputs\InputBus.cpp" ^
   "%STUDIO%\inputs\OnsetDetect.cpp" ^
   "%STUDIO%\inputs\KeyMap.cpp" ^
   "%STUDIO%\presets\DevicePreset.cpp" ^
   "%STUDIO%\presets\PresetRegistry.cpp" ^
   "%STUDIO%\presets\EffectRegistry.cpp" ^
   "%STUDIO%\presets\PresetBundle.cpp" ^
   "%STUDIO%\scene\SceneResolver.cpp" ^
   "%STUDIO%\config\StudioConfig.cpp" ^
   "%STUDIO%\config\ConfigMigration.cpp" ^
   "%STUDIO%\editor\EditorController.cpp" ^
   "%STUDIO%\editor\TransformCommands.cpp" ^
   /Fo:out\perf\ /Fe:out\perf\studio_perf.exe
if errorlevel 1 exit /b %errorlevel%
out\perf\studio_perf.exe
exit /b %errorlevel%
