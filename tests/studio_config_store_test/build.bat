@echo off
:: Build + run the ConfigStore test. No deploy - the exe links
:: against Qt DLLs found via PATH only.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\jom;%PATH%
cd /d %~dp0
qmake studio_config_store_test.pro
if errorlevel 1 exit /b %errorlevel%
jom release
if errorlevel 1 exit /b %errorlevel%
release\studio_config_store_test.exe
exit /b %errorlevel%
