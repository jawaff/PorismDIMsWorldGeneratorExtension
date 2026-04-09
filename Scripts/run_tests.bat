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

set "ARG1=%~1"
set "ARG2=%~2"

if /I "%ARG1%"=="--no-build" (
	set "SKIP_EDITOR_BUILD=1"
) else if /I "%ARG1%"=="--no-editor-build" (
	set "SKIP_EDITOR_BUILD=1"
) else if /I "%ARG1%"=="--build" (
	set "SKIP_EDITOR_BUILD=0"
) else if not "%ARG1%"=="" (
	set "TEST_FILTER=%ARG1%"
)

if /I "%ARG2%"=="--no-build" (
	set "SKIP_EDITOR_BUILD=1"
) else if /I "%ARG2%"=="--no-editor-build" (
	set "SKIP_EDITOR_BUILD=1"
) else if /I "%ARG2%"=="--build" (
	set "SKIP_EDITOR_BUILD=0"
) else if not "%ARG2%"=="" (
	set "TEST_FILTER=%ARG2%"
)

if not exist "%PORISM_EXTENSION_LOG_DIR%" mkdir "%PORISM_EXTENSION_LOG_DIR%"
if not exist "%PORISM_EXTENSION_REPORT_DIR%" mkdir "%PORISM_EXTENSION_REPORT_DIR%"

set "TEST_LOG_RUN=%PORISM_EXTENSION_LOG_DIR%\PorismExtension_TestRun.log"
if exist "%TEST_LOG_RUN%" del /q "%TEST_LOG_RUN%"

if "%SKIP_EDITOR_BUILD%"=="0" (
	"%UE_UBT%" %PORISM_EXTENSION_EDITOR_TARGET% Win64 Development -Project="%PORISM_EXTENSION_HOST_PROJECT%" -WaitMutex
	if errorlevel 1 (
		echo Editor build failed. Aborting plugin tests.
		exit /b 1
	)
)

%UE_EDITOR_CMD% "%PORISM_EXTENSION_HOST_PROJECT%" %PORISM_EXTENSION_TEST_MAP% -ini:Engine=Config/Tests/DefaultEngine.ini -ini:Editor=Config/Tests/DefaultEditor.ini -Unattended -NoSound -NullRHI -NoXR -nohmd -nopause -NoSplash -RenderOffscreen -nocontentbrowser -nosteam -ExecCmds="Automation RunTests %TEST_FILTER%; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath="%PORISM_EXTENSION_REPORT_DIR%" -AbsLog="%TEST_LOG_RUN%" -LogCmds="LogAutomationTest VeryVerbose,LogAutomationController Verbose,LogAutomationCommandLine Verbose,LogModuleManager Verbose,LogPluginManager Verbose"
set "UE_TEST_EXIT=%ERRORLEVEL%"

set "REPORT_JSON=%PORISM_EXTENSION_REPORT_DIR%\index.json"
if exist "%REPORT_JSON%" (
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"succeeded\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_SUCCEEDED=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"succeededWithWarnings\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_WARNINGS=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"failed\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_FAILED=%%A"
	for /f "tokens=2 delims=:" %%A in ('findstr /R /C:"\"notRun\"[ ]*:" "%REPORT_JSON%"') do set "REPORT_NOTRUN=%%A"

	for /f "tokens=1 delims=, " %%A in ("!REPORT_SUCCEEDED!") do set "REPORT_SUCCEEDED=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_WARNINGS!") do set "REPORT_WARNINGS=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_FAILED!") do set "REPORT_FAILED=%%A"
	for /f "tokens=1 delims=, " %%A in ("!REPORT_NOTRUN!") do set "REPORT_NOTRUN=%%A"

	echo Plugin tests: !REPORT_SUCCEEDED! succeeded, !REPORT_FAILED! failed, !REPORT_WARNINGS! warnings, !REPORT_NOTRUN! not run.
	if not "!REPORT_FAILED!"=="0" exit /b 1
) else (
	echo Test report not found at %REPORT_JSON%
)

echo Plugin test log: %TEST_LOG_RUN%
if not "%UE_TEST_EXIT%"=="0" exit /b %UE_TEST_EXIT%
