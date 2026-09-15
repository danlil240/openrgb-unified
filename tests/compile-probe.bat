@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
cd /d C:\Users\daniel\tools\openrgb-unified\plugins\DesktopLightingStudio
set INC=/I. /I..\..\OpenRGB /I..\..\OpenRGB\RGBController /I..\..\OpenRGB\dependencies\json /IC:\Qt\6.8.3\msvc2022_64\include /IC:\Qt\6.8.3\msvc2022_64\include\QtCore /IC:\Qt\6.8.3\msvc2022_64\include\QtGui
cl /nologo /EHsc /std:c++17 /W3 /c %INC% inputs\AudioLoopback.cpp /Fo:%TEMP%\al.obj
echo --- AudioLoopback: %errorlevel% ---
cl /nologo /EHsc /std:c++17 /W3 /c %INC% inputs\KeyHook.cpp /Fo:%TEMP%\kh.obj
echo --- KeyHook: %errorlevel% ---
