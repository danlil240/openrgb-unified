@echo off
::---------------------------------------------------------::
:: DesktopLightingStudio packaging verification            ::
::                                                          ::
:: Checks the file-side manifest in PACKAGING.md against   ::
:: out\ — expected vs present, exit 1 on any miss. Read-  ::
:: only: never copies, never deploys. Run after            ::
:: build-plugin.bat (windeployqt) has populated out\.      ::
::---------------------------------------------------------::
setlocal EnableDelayedExpansion
cd /d %~dp0
set "MISSING=0"
set "CHECKED=0"

call :need_file out\DesktopLightingStudio.dll

:: Qt runtime DLLs (flat in out\)
for %%d in (Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6OpenGL Qt6Svg Qt6Qml Qt6QmlMeta Qt6QmlModels Qt6QmlWorkerScript Qt6Quick Qt6QuickLayouts Qt6QuickShapes Qt6QuickEffects Qt6QuickTemplates2 Qt6QuickControls2 Qt6QuickControls2Impl Qt6QuickWidgets Qt6ShaderTools Qt6Quick3D Qt6Quick3DUtils Qt6Quick3DRuntimeRender Qt6Quick3DHelpers Qt6Quick3DHelpersImpl Qt6QuickControls2Basic Qt6QuickControls2BasicStyleImpl Qt6QuickControls2Fusion Qt6QuickControls2FusionStyleImpl Qt6QuickControls2Imagine Qt6QuickControls2ImagineStyleImpl Qt6QuickControls2Material Qt6QuickControls2MaterialStyleImpl Qt6QuickControls2Universal Qt6QuickControls2UniversalStyleImpl Qt6QuickControls2WindowsStyleImpl Qt6QuickControls2FluentWinUI3StyleImpl) do (
    set /a CHECKED+=1
    if exist "out\%%d.dll" (echo   ok      out\%%d.dll) else (echo   MISSING out\%%d.dll & set /a MISSING+=1)
)

:: Qt plugins
for %%d in (platforms\qwindows styles\qmodernwindowsstyle imageformats\qgif imageformats\qico imageformats\qjpeg imageformats\qsvg iconengines\qsvgicon tls\qcertonlybackend tls\qschannelbackend networkinformation\qnetworklistmanager generic\qtuiotouchplugin) do (
    set /a CHECKED+=1
    if exist "out\%%d.dll" (echo   ok      out\%%d.dll) else (echo   MISSING out\%%d.dll & set /a MISSING+=1)
)

call :need_file out\opengl32sw.dll
call :need_file out\dxcompiler.dll
call :need_file out\dxil.dll

:: QML module dirs — QtQuick3D.Helpers is the scene's
:: ExtendedSceneEnvironment; a missing dir ships a black viewport.
for %%d in (QtQml QtQml\Models QtQml\WorkerScript QtQml\XmlListModel QtQuick\Controls QtQuick\Controls\Basic QtQuick\Controls\Fusion QtQuick\Controls\Imagine QtQuick\Controls\Material QtQuick\Controls\Universal QtQuick\Controls\Windows QtQuick\Controls\FluentWinUI3 QtQuick\Controls\impl QtQuick\Dialogs QtQuick\Effects QtQuick\Layouts QtQuick\Shapes QtQuick\Templates QtQuick\Window QtQuick3D QtQuick3D\Helpers QtQuick3D\AssetUtils QtQuick3D\Effects QtQuick3D\Particles3D) do (
    set /a CHECKED+=1
    if exist "out\qml\%%d\" (echo   ok      out\qml\%%d\) else (echo   MISSING out\qml\%%d\ & set /a MISSING+=1)
)

echo ------------------------------------------------------------
if "%MISSING%"=="0" (
    echo PACKAGE OK - %CHECKED% manifest entries present in out\
    endlocal
    exit /b 0
)
echo PACKAGE INCOMPLETE - %MISSING% of %CHECKED% manifest entries missing
endlocal
exit /b 1

:need_file
set /a CHECKED+=1
if exist "%~1" (echo   ok      %~1) else (echo   MISSING %~1 & set /a MISSING+=1)
exit /b 0
