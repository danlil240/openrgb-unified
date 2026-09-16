@echo off
:: Run the pure-QML controller tests through the SIGNED
:: qmltestrunner.exe. Smart App Control blocks every freshly built
:: unsigned exe (editor_qml_test.exe, studio_*_test.exe); Qt's own
:: binaries are reputation-allowed, so this is the runtime path that
:: still executes the gesture-state-machine regression tests.
:: Directory input — runs every tst_*.qml here (tst_controllers.qml
:: plus the tst_smoke.qml runner probe).
"C:\Qt\6.8.3\msvc2022_64\bin\qmltestrunner.exe" -input "%~dp0."
exit /b %errorlevel%
