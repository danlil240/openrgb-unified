@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /EHsc /std:c++17 windows_launch_policy_test.cpp /Fo:out\windows_launch_policy_test.obj /Fe:out\windows_launch_policy_test.exe
if errorlevel 1 exit /b %errorlevel%
out\windows_launch_policy_test.exe
exit /b %errorlevel%
