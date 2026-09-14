@echo off
echo Deploying new OpenRGB build...
taskkill /f /im OpenRGB.exe 2>nul
timeout /t 2 /nobreak >nul
copy /y "C:\Users\daniel\tools\openrgb-unified\OpenRGB\release\OpenRGB.exe" "C:\Users\daniel\tools\openrgb-unified\OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe"
cd /d "C:\Users\daniel\tools\openrgb-unified\OpenRGB\OpenRGB Windows 64-bit"
start "" "C:\Users\daniel\tools\openrgb-unified\OpenRGB\OpenRGB Windows 64-bit\OpenRGB.exe" --server --localconfig
echo Done. Server starting with Lian Li wireless support.
