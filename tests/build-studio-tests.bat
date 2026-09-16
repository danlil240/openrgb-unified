@echo off
:: Build + run the Qt-free Desktop Lighting Studio scene/config tests.
:: NOTE: Smart App Control caches per-path verdicts on unsigned exes
:: here — each suite links into its own out\<suite>\ dir so a stale
:: deny verdict can't poison a whole directory.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\scene mkdir out\scene
if not exist out\config mkdir out\config
if not exist out\presets mkdir out\presets
if not exist out\editor mkdir out\editor
set STUDIO=%~dp0..\plugins\DesktopLightingStudio
set JSON=%~dp0..\OpenRGB\dependencies\json
:: Shared source list — scene core + effects + the v3 type stack
:: (presets, resolver, migration) so every suite sees one truth.
set SCENE_SRC=^
   "%STUDIO%\scene\SceneTypes.cpp" ^
   "%STUDIO%\scene\SceneGraph.cpp" ^
   "%STUDIO%\scene\EmitterLayout.cpp" ^
   "%STUDIO%\scene\SceneJson.cpp" ^
   "%STUDIO%\scene\BindingResolver.cpp" ^
   "%STUDIO%\scene\DefaultDesk.cpp" ^
   "%STUDIO%\effects\EffectTypes.cpp" ^
   "%STUDIO%\effects\EffectEngine.cpp" ^
   "%STUDIO%\effects\Presets.cpp" ^
   "%STUDIO%\inputs\InputBus.cpp" ^
   "%STUDIO%\inputs\OnsetDetect.cpp" ^
   "%STUDIO%\inputs\KeyMap.cpp"
set V3_SRC=^
   "%STUDIO%\presets\DevicePreset.cpp" ^
   "%STUDIO%\presets\PresetRegistry.cpp" ^
   "%STUDIO%\scene\SceneResolver.cpp" ^
   "%STUDIO%\config\StudioConfig.cpp" ^
   "%STUDIO%\config\ConfigMigration.cpp"
:: Qt-free editor core — SceneObjectModel stays plugin-only (Qt).
set EDITOR_SRC=^
   "%STUDIO%\editor\EditorController.cpp" ^
   "%STUDIO%\editor\TransformCommands.cpp"
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_scene_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\scene\ /Fe:out\scene\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
out\scene\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_config_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\config\ /Fe:out\config\studio_config_test.exe
if errorlevel 1 exit /b %errorlevel%
out\config\studio_config_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   device_preset_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\presets\ /Fe:out\presets\studio_preset_test.exe
if errorlevel 1 exit /b %errorlevel%
out\presets\studio_preset_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_editor_test.cpp ^
   %SCENE_SRC% %V3_SRC% %EDITOR_SRC% ^
   /Fo:out\editor\ /Fe:out\editor\studio_editor_test.exe
if errorlevel 1 exit /b %errorlevel%
out\editor\studio_editor_test.exe
exit /b %errorlevel%
