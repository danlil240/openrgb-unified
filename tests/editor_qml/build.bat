@echo off
:: Build + run the editor_qml interaction test. No deploy — the exe
:: links against Qt DLLs found via PATH only. On a Smart App Control
:: per-path block, falls back to a previously-evaluated trampoline
:: path under tests\out (same trick as build-studio-tests.bat).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\jom;%PATH%
cd /d %~dp0
qmake editor_qml.pro
if errorlevel 1 exit /b %errorlevel%
jom release
if errorlevel 1 exit /b %errorlevel%
release\editor_qml_test.exe
if not errorlevel 1 exit /b 0
if not exist ..\out mkdir ..\out
copy /y release\editor_qml_test.exe ..\out\editor_qml_run.exe >nul
..\out\editor_qml_run.exe
if not errorlevel 1 exit /b 0
copy /y release\editor_qml_test.exe ..\out\studio_scene_test2.exe >nul
..\out\studio_scene_test2.exe
exit /b %errorlevel%
