@echo off
:: Build + run the Qt-free Desktop Lighting Studio scene tests.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out mkdir out
set STUDIO=%~dp0..\plugins\DesktopLightingStudio
set JSON=%~dp0..\OpenRGB\dependencies\json
cl /nologo /EHsc /std:c++17 /I"%STUDIO%" /I"%JSON%" ^
   studio_scene_test.cpp ^
   "%STUDIO%\scene\SceneTypes.cpp" ^
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
   /Fo:out\ /Fe:out\studio_scene_test.exe
if errorlevel 1 exit /b %errorlevel%
out\studio_scene_test.exe
exit /b %errorlevel%
