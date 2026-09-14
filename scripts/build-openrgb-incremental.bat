::---------------------------------------------------------::
:: Incremental OpenRGB rebuild                               ::
::                                                           ::
:: Rebuilds only changed sources (release/OpenRGB.exe)       ::
:: without the full build-windows.bat deploy/move steps.     ::
::---------------------------------------------------------::
@echo off
set "PATH=%PATH%;C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\jom"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d %~dp0..\OpenRGB
qmake OpenRGB.pro CONFIG-=debug_and_release CONFIG+=release
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
jom
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
echo Incremental build done: %CD%\release\OpenRGB.exe
