@echo off
setlocal EnableDelayedExpansion

rem Usage:
rem   Plugins\PorismDIMsWorldGeneratorExtension\Scripts\run_tests.bat [BuildFlag] [TestFilter]
rem   BuildFlag options:
rem     --build            (default)
rem     --no-build
rem     --no-editor-build  (alias for --no-build)
rem   TestFilter:
rem     Defaults to the whole PorismDIMsWorldGeneratorExtensionTests module via the
rem     shared "PorismExtension" automation prefix. Pass a narrower filter explicitly
rem     only when you are intentionally targeting a subset.

call "%~dp0env.bat"

set "SKIP_EDITOR_BUILD=0"
set "TEST_FILTER=%PORISM_EXTENSION_TEST_FILTER%"
if "%TEST_FILTER%"=="" set "TEST_FILTER=PorismExtension"
set "FILTER_SET=0"

for %%A in (%*) do (
	if /I "%%~A"=="--no-build" (
		set "SKIP_EDITOR_BUILD=1"
	) else if /I "%%~A"=="--no-editor-build" (
		set "SKIP_EDITOR_BUILD=1"
	) else if /I "%%~A"=="--build" (
		set "SKIP_EDITOR_BUILD=0"
	) else if "!FILTER_SET!"=="0" (
		set "TEST_FILTER=%%~A"
		set "FILTER_SET=1"
	)
)

if not exist "%PORISM_EXTENSION_LOG_DIR%" mkdir "%PORISM_EXTENSION_LOG_DIR%"
if not exist "%PORISM_EXTENSION_REPORT_DIR%" mkdir "%PORISM_EXTENSION_REPORT_DIR%"

set "TEST_LOG_RUN=%PORISM_EXTENSION_LOG_DIR%\PorismExtension_TestRun.log"
set "LOCK_DIR=%PORISM_EXTENSION_PLUGIN_ROOT%\Saved\TestRun.lock"

mkdir "%LOCK_DIR%" 2>nul
if errorlevel 1 (
	echo Another PorismExtension test run is already using the shared log/report paths.
	echo Wait for it to finish, or remove the stale lock at %LOCK_DIR% if no test process is still running.
	exit /b 1
)

if exist "%TEST_LOG_RUN%" del /q "%TEST_LOG_RUN%"

if "%SKIP_EDITOR_BUILD%"=="0" (
	"%UE_UBT%" %PORISM_EXTENSION_EDITOR_TARGET% Win64 Development -Project="%PORISM_EXTENSION_HOST_PROJECT%" -WaitMutex
	if errorlevel 1 (
		echo Editor build failed. Aborting plugin tests.
		call :release_lock
		exit /b 1
	)
)

%UE_EDITOR_CMD% "%PORISM_EXTENSION_HOST_PROJECT%" %PORISM_EXTENSION_TEST_MAP% -ini:Engine=Config/Tests/DefaultEngine.ini -ini:Editor=Config/Tests/DefaultEditor.ini -Unattended -NoSound -NullRHI -NoXR -nohmd -nopause -NoSplash -RenderOffscreen -nocontentbrowser -nosteam -ExecCmds="Automation RunTests %TEST_FILTER%" -TestExit="Automation Test Queue Empty" -ReportExportPath="%PORISM_EXTENSION_REPORT_DIR%" -AbsLog="%TEST_LOG_RUN%" -LogCmds="LogAutomationTest VeryVerbose,LogAutomationController Verbose,LogAutomationCommandLine Verbose,LogModuleManager Verbose,LogPluginManager Verbose"
set "UE_TEST_EXIT=%ERRORLEVEL%"

set "REPORT_JSON=%PORISM_EXTENSION_REPORT_DIR%\index.json"
if not "%UE_TEST_EXIT%"=="0" (
	echo Unreal automation exited with code %UE_TEST_EXIT%.
	if exist "%REPORT_JSON%" (
		echo Partial automation report detected at %REPORT_JSON%.
	)
	echo Plugin test log: %TEST_LOG_RUN%
	findstr /C:"No automation tests matched '%TEST_FILTER%'" "%TEST_LOG_RUN%" >nul
	if not errorlevel 1 (
		echo No tests matched filter '%TEST_FILTER%'. In UE 5.7, use a prefix filter like "PorismExtension" instead of "PorismExtension.*".
	)
	call :release_lock
	exit /b %UE_TEST_EXIT%
)

if exist "%REPORT_JSON%" (
	set "REPORT_SUCCEEDED=0"
	set "REPORT_WARNINGS=0"
	set "REPORT_FAILED=0"
	set "REPORT_NOTRUN=0"

	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"succeeded\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_SUCCEEDED=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"succeededWithWarnings\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_WARNINGS=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"failed\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_FAILED=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"notRun\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_NOTRUN=%%A"

	for /f "tokens=1 delims=, " %%A in ("!REPORT_SUCCEEDED!") do set "REPORT_SUCCEEDED=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_WARNINGS!") do set "REPORT_WARNINGS=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_FAILED!") do set "REPORT_FAILED=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_NOTRUN!") do set "REPORT_NOTRUN=%%A"

	set /a REPORT_TOTAL=!REPORT_SUCCEEDED!+!REPORT_WARNINGS!+!REPORT_FAILED!+!REPORT_NOTRUN!
	set "PERFORMED_COUNT="
	for /f "tokens=7" %%A in ('findstr /C:"tests performed." "%TEST_LOG_RUN%"') do set "PERFORMED_COUNT=%%A"

	if "!PERFORMED_COUNT!"=="" (
		echo Could not determine how many tests Unreal reports as performed from %TEST_LOG_RUN%.
		call :release_lock
		exit /b 1
	)

	if not "!REPORT_TOTAL!"=="!PERFORMED_COUNT!" (
		echo Plugin automation report mismatch: JSON totals !REPORT_TOTAL! tests but Unreal reported !PERFORMED_COUNT! performed.
		echo This usually means the exported report is partial or inconsistent.
		echo Plugin test log: %TEST_LOG_RUN%
		call :release_lock
		exit /b 1
	)

	if "!REPORT_TOTAL!"=="0" (
		echo No plugin tests executed for filter '%TEST_FILTER%'.
		call :release_lock
		exit /b 1
	)

	echo Plugin tests: !REPORT_SUCCEEDED! succeeded, !REPORT_FAILED! failed, !REPORT_WARNINGS! warnings, !REPORT_NOTRUN! not run.
	if not "!REPORT_FAILED!"=="0" (
		call :release_lock
		exit /b 1
	)
) else (
	echo Test report not found at %REPORT_JSON%
	call :release_lock
	exit /b 1
)

echo Plugin test log: %TEST_LOG_RUN%
call :release_lock
exit /b 0

:release_lock
if exist "%LOCK_DIR%" rmdir /s /q "%LOCK_DIR%"
