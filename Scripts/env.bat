@echo off
setlocal

rem Shared environment for Porism DIMs World Generator Extension scripts.
rem Machine-specific settings belong in Scripts\env.local.bat.

set "PORISM_EXTENSION_PLUGIN_ROOT=%~dp0.."
rem Default to the full plugin-local automation module. Individual scripts can still pass a narrower filter.
set "PORISM_EXTENSION_TEST_FILTER=PorismExtension"
set "PORISM_EXTENSION_LOG_DIR=%PORISM_EXTENSION_PLUGIN_ROOT%\Saved\Logs"
set "PORISM_EXTENSION_REPORT_DIR=%PORISM_EXTENSION_PLUGIN_ROOT%\Saved\TestsReport"

if exist "%~dp0env.local.bat" call "%~dp0env.local.bat"

if "%UE_ENGINE_ROOT%"=="" (
	echo Missing UE_ENGINE_ROOT. Define it in "%~dp0env.local.bat".
	exit /b 1
)

if "%PORISM_EXTENSION_HOST_PROJECT%"=="" (
	echo Missing PORISM_EXTENSION_HOST_PROJECT. Define it in "%~dp0env.local.bat".
	exit /b 1
)

if "%PORISM_EXTENSION_EDITOR_TARGET%"=="" (
	echo Missing PORISM_EXTENSION_EDITOR_TARGET. Define it in "%~dp0env.local.bat".
	exit /b 1
)

if "%PORISM_EXTENSION_TEST_MAP%"=="" (
	echo Missing PORISM_EXTENSION_TEST_MAP. Define it in "%~dp0env.local.bat".
	exit /b 1
)

set "UE_EDITOR_CMD=%UE_ENGINE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "UE_UBT=%UE_ENGINE_ROOT%\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"

endlocal & (
	set "UE_ENGINE_ROOT=%UE_ENGINE_ROOT%"
	set "UE_EDITOR_CMD=%UE_EDITOR_CMD%"
	set "UE_UBT=%UE_UBT%"
	set "PORISM_EXTENSION_PLUGIN_ROOT=%PORISM_EXTENSION_PLUGIN_ROOT%"
	set "PORISM_EXTENSION_HOST_PROJECT=%PORISM_EXTENSION_HOST_PROJECT%"
	set "PORISM_EXTENSION_EDITOR_TARGET=%PORISM_EXTENSION_EDITOR_TARGET%"
	set "PORISM_EXTENSION_TEST_FILTER=%PORISM_EXTENSION_TEST_FILTER%"
	set "PORISM_EXTENSION_TEST_MAP=%PORISM_EXTENSION_TEST_MAP%"
	set "PORISM_EXTENSION_LOG_DIR=%PORISM_EXTENSION_LOG_DIR%"
	set "PORISM_EXTENSION_REPORT_DIR=%PORISM_EXTENSION_REPORT_DIR%"
)
