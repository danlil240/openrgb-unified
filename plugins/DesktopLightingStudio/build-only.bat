::---------------------------------------------------------::
:: DesktopLightingStudio plugin build - no deploy            ::
::                                                           ::
:: qmake + jom only. Use for compile validation; run         ::
:: deploy-only.bat afterwards when the DLL should be         ::
:: copied into the local OpenRGB build.                      ::
::---------------------------------------------------------::
@echo off
setlocal

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set "PATH=%PATH%;%QT_DIR%\bin;C:\Qt\jom"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul

cd /d %~dp0

qmake DesktopLightingStudio.pro CONFIG-=debug_and_release CONFIG+=release
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

:: Force moc re-run: metadata.json is baked by moc but not tracked
:: as a dependency, so edits would silently keep the old metadata.
if exist build\moc del /q build\moc\*
if not exist build\moc mkdir build\moc

jom
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

echo BUILD OK (not deployed)
endlocal
