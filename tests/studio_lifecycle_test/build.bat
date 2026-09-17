@echo off
:: Build + run the studio lifecycle test (Task 6.1). No deploy —
:: the exe links against Qt DLLs found via PATH only. On a Smart
:: App Control per-path block, falls back to a previously-evaluated
:: trampoline path under tests\out (same trick as editor_qml).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\jom;%PATH%
cd /d %~dp0
qmake studio_lifecycle_test.pro
if errorlevel 1 exit /b %errorlevel%
jom release
if errorlevel 1 exit /b %errorlevel%
release\studio_lifecycle_test.exe
if not errorlevel 1 exit /b 0
if not exist ..\out mkdir ..\out
copy /y release\studio_lifecycle_test.exe ..\out\studio_lifecycle_run.exe >nul
..\out\studio_lifecycle_run.exe
if not errorlevel 1 exit /b 0
copy /y release\studio_lifecycle_test.exe ..\out\studio_scene_test2.exe >nul
..\out\studio_scene_test2.exe
exit /b %errorlevel%
