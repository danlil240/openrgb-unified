@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Qt\jom;%PATH%
cd /d %~dp0
qmake quickwidget_probe.pro
jom
