@echo off
:: Deploy-only step of build-plugin.bat — run when the plugin DLL is
:: already built but the target OpenRGB copy is stale (e.g. xcopy hit
:: a sharing violation on a running host). Uses xcopy/cmd only — on
:: this machine Smart App Control blocked the same bytes copied via
:: PowerShell Copy-Item.
setlocal
cd /d %~dp0
set "PLUGIN_DST=%~dp0..\..\OpenRGB\OpenRGB Windows 64-bit\plugins\DesktopLightingStudio"
if not exist "%PLUGIN_DST%" mkdir "%PLUGIN_DST%"
xcopy /y /e /i out\* "%PLUGIN_DST%\" >nul || exit /b 4
xcopy /y /e /i ui "%PLUGIN_DST%\ui\" >nul || exit /b 4
set "EXE_DIR=%~dp0..\..\OpenRGB\OpenRGB Windows 64-bit"
xcopy /y /d out\Qt6*.dll "%EXE_DIR%\" >nul || exit /b 4
xcopy /y /d out\dxcompiler.dll "%EXE_DIR%\" >nul || exit /b 4
xcopy /y /d out\dxil.dll "%EXE_DIR%\" >nul || exit /b 4
xcopy /y /d out\opengl32sw.dll "%EXE_DIR%\" >nul || exit /b 4
echo DEPLOYED to %PLUGIN_DST%
endlocal
