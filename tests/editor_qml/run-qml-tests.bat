@echo off
:: Run the pure-QML controller tests through the SIGNED
:: qmltestrunner.exe. Smart App Control blocks every freshly built
:: unsigned exe (editor_qml_test.exe, studio_*_test.exe); Qt's own
:: binaries are reputation-allowed, so this is the runtime path that
:: still executes the gesture-state-machine regression tests.
:: Directory input — runs every tst_*.qml here.
::
:: Loudness fix (task 6.2): on this box qmltestrunner writes nothing
:: to a redirected stdout — a bare invocation exits 0 in silence and
:: a broken/empty harness would look identical to a pass. Output now
:: goes to a log file via -o, the log is echoed back, and a missing
:: or Totals-less log is a LOUD failure so a silent pass can never
:: mask a dead runner.
set "QMLRUN=C:\Qt\6.8.3\msvc2022_64\bin\qmltestrunner.exe"
:: log lives under the gitignored tests/out/ scratch dir
if not exist "%~dp0..\out" mkdir "%~dp0..\out"
set "LOG=%~dp0..\out\qml-tests.log"
if exist "%LOG%" del /q "%LOG%"

"%QMLRUN%" -input "%~dp0." -o "%LOG%",txt
set "RC=%errorlevel%"

if not exist "%LOG%" (
    echo FAIL: qmltestrunner produced no log file - runner is dead
    exit /b 1
)
type "%LOG%"
findstr /r /c:"Totals: *[1-9][0-9]* passed" "%LOG%" >nul
if errorlevel 1 (
    echo FAIL: qmltestrunner log has no Totals line with a nonzero pass count
    exit /b 1
)
if not "%RC%"=="0" (
    echo FAIL: qmltestrunner exited %RC% - see log above
    exit /b %RC%
)
echo QML TESTS OK
exit /b 0
