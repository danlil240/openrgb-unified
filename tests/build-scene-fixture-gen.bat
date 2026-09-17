@echo off
:: Compile + run the scene fixture generator (plain cl, Qt-free).
:: Regenerates tests\fixtures\scenes\<name>\studio.json — the output
:: is deterministic and committed; rerun only when the recipe in
:: scene_fixture_gen.cpp intentionally changes.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out\fixgen mkdir out\fixgen
set STUDIO=%~dp0..\plugins\DesktopLightingStudio
set JSON=%~dp0..\OpenRGB\dependencies\json
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   scene_fixture_gen.cpp ^
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
   /Fo:out\fixgen\ /Fe:out\fixgen\scene_fixture_gen.exe
if errorlevel 1 exit /b %errorlevel%
out\fixgen\scene_fixture_gen.exe
exit /b %errorlevel%
