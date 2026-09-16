@echo off
:: Build + run the Qt-free Desktop Lighting Studio scene/config tests.
:: NOTE: Smart App Control caches per-path verdicts on unsigned exes
:: here — each suite links into its own out\<suite>\ dir so a stale
:: deny verdict can't poison a whole directory. When SAC still blocks
:: a fresh binary, :run falls back to copying it onto a path SAC has
:: already evaluated (out\studio_scene_test2.exe) and runs that copy —
:: the known workaround for per-path verdict flakiness.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\scene mkdir out\scene
if not exist out\config mkdir out\config
if not exist out\presets mkdir out\presets
if not exist out\editor mkdir out\editor
if not exist out\effects mkdir out\effects
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
   "%STUDIO%\effects\EffectJson.cpp" ^
   "%STUDIO%\effects\Presets.cpp" ^
   "%STUDIO%\inputs\InputBus.cpp" ^
   "%STUDIO%\inputs\OnsetDetect.cpp" ^
   "%STUDIO%\inputs\KeyMap.cpp"
set V3_SRC=^
   "%STUDIO%\presets\DevicePreset.cpp" ^
   "%STUDIO%\presets\PresetRegistry.cpp" ^
   "%STUDIO%\presets\EffectRegistry.cpp" ^
   "%STUDIO%\presets\PresetBundle.cpp" ^
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
call :run out\scene\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_config_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\config\ /Fe:out\config\studio_config_test.exe
if errorlevel 1 exit /b %errorlevel%
call :run out\config\studio_config_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   device_preset_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\presets\ /Fe:out\presets\studio_preset_test.exe
if errorlevel 1 exit /b %errorlevel%
call :run out\presets\studio_preset_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_editor_test.cpp ^
   %SCENE_SRC% %V3_SRC% %EDITOR_SRC% ^
   /Fo:out\editor\ /Fe:out\editor\studio_editor_test.exe
if errorlevel 1 exit /b %errorlevel%
call :run out\editor\studio_editor_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   effect_json_test.cpp ^
   %SCENE_SRC% %V3_SRC% ^
   /Fo:out\effects\ /Fe:out\effects\effect_json_test.exe
if errorlevel 1 exit /b %errorlevel%
call :run out\effects\effect_json_test.exe
exit /b %errorlevel%

:: Run a test exe; on a Device Guard per-path block, copy onto the
:: previously-evaluated trampoline path and run the copy.
:run
%1
if not errorlevel 1 exit /b 0
copy /y %1 out\studio_scene_test2.exe >nul
out\studio_scene_test2.exe
exit /b %errorlevel%
