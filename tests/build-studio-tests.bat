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
set STUDIO=%~dp0..\plugins\DesktopLightingStudio
set JSON=%~dp0..\OpenRGB\dependencies\json
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_scene_test.cpp ^
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
   "%STUDIO%\inputs\KeyMap.cpp" ^
   /Fo:out\scene\ /Fe:out\scene\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
out\scene\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_config_test.cpp ^
   "%STUDIO%\scene\SceneTypes.cpp" ^
   "%STUDIO%\scene\SceneGraph.cpp" ^
   "%STUDIO%\scene\EmitterLayout.cpp" ^
   "%STUDIO%\scene\SceneJson.cpp" ^
   "%STUDIO%\scene\BindingResolver.cpp" ^
   "%STUDIO%\scene\DefaultDesk.cpp" ^
   "%STUDIO%\config\StudioConfig.cpp" ^
   "%STUDIO%\config\ConfigMigration.cpp" ^
   "%STUDIO%\effects\EffectTypes.cpp" ^
   "%STUDIO%\effects\EffectEngine.cpp" ^
   "%STUDIO%\effects\Presets.cpp" ^
   "%STUDIO%\inputs\InputBus.cpp" ^
   "%STUDIO%\inputs\OnsetDetect.cpp" ^
   "%STUDIO%\inputs\KeyMap.cpp" ^
   /Fo:out\config\ /Fe:out\config\studio_config_test.exe
if errorlevel 1 exit /b %errorlevel%
out\config\studio_config_test.exe
exit /b %errorlevel%
