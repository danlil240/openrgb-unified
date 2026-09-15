::---------------------------------------------------------::
:: DesktopLightingStudio plugin build + deploy               ::
::                                                           ::
:: Builds the API-5 plugin against the pinned Qt/MSVC        ::
:: toolchain, gathers Qt + QML module dependencies with      ::
:: windeployqt, and copies the result into the local         ::
:: OpenRGB build's plugins\ folder (scanned as system        ::
:: plugins by PluginManager).                                ::
::---------------------------------------------------------::
@echo off
setlocal

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set "PATH=%PATH%;%QT_DIR%\bin;C:\Qt\jom"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

cd /d %~dp0

qmake DesktopLightingStudio.pro CONFIG-=debug_and_release CONFIG+=release
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

:: Force moc re-run: metadata.json is baked by moc but not tracked
:: as a dependency, so edits would silently keep the old metadata.
if exist build\moc rmdir /s /q build\moc

jom
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

windeployqt --no-patchqt --no-translations --no-system-d3d-compiler --no-compiler-runtime --qmldir ui out\DesktopLightingStudio.dll
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

set "PLUGIN_DST=%~dp0..\..\OpenRGB\OpenRGB Windows 64-bit\plugins\DesktopLightingStudio"
if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
xcopy /y /e /i out\* "%PLUGIN_DST%\" >nul

echo Deployed plugin to %PLUGIN_DST%
endlocal
