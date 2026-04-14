@echo off
setlocal EnableExtensions EnableDelayedExpansion
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

call "%REPO_ROOT%\devenv.cmd"
if errorlevel 1 exit /b %errorlevel%

set "ITERATIONS="
set "RENDER_SCALE="
set "COMMAND=run"
set "REQUEST_ELEVATION=0"
set "IS_ELEVATED_RELAUNCH=0"
set "REQUEST_ID="
set "REQUEST_DIR="
set "DAEMON_ROOT=%REPO_ROOT%\build\profile_benchmark_daemon"
set "DAEMON_STATUS=%DAEMON_ROOT%\status.env"
set "DAEMON_READY=%DAEMON_ROOT%\ready.flag"
set "DAEMON_STOP=%DAEMON_ROOT%\stop.flag"
set "DAEMON_SCRIPT=%REPO_ROOT%\tools\profile_benchmark_daemon.ps1"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="/daemon-start" (
    set "COMMAND=daemon_start"
    shift
    goto parse_args
)
if /i "%~1"=="/daemon-stop" (
    set "COMMAND=daemon_stop"
    shift
    goto parse_args
)
if /i "%~1"=="/daemon-status" (
    set "COMMAND=daemon_status"
    shift
    goto parse_args
)
if /i "%~1"=="/run-request" (
    set "COMMAND=run_request"
    set "REQUEST_DIR=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="/elevate" (
    set "REQUEST_ELEVATION=1"
    shift
    goto parse_args
)
if /i "%~1"=="/elevated" (
    set "IS_ELEVATED_RELAUNCH=1"
    shift
    goto parse_args
)
if not defined ITERATIONS (
    set "ITERATIONS=%~1"
) else if not defined RENDER_SCALE (
    set "RENDER_SCALE=%~1"
)
shift
goto parse_args

:args_done
if not defined ITERATIONS set "ITERATIONS=600"
if not defined RENDER_SCALE set "RENDER_SCALE=2"

if /i "%COMMAND%"=="daemon_start" goto daemon_start
if /i "%COMMAND%"=="daemon_stop" goto daemon_stop
if /i "%COMMAND%"=="daemon_status" goto daemon_status
if /i "%COMMAND%"=="run_request" goto run_request
goto run_default

:daemon_start
if not exist "%DAEMON_SCRIPT%" (
    echo Missing daemon helper script: "%DAEMON_SCRIPT%".
    exit /b 1
)
if exist "%DAEMON_READY%" (
    call :load_daemon_status
    if not errorlevel 1 (
        echo Benchmark daemon already running with PID %DAEMON_PID%.
        exit /b 0
    )
)
if not exist "%DAEMON_ROOT%" mkdir "%DAEMON_ROOT%"
del /q "%DAEMON_READY%" >nul 2>nul
del /q "%DAEMON_STOP%" >nul 2>nul
del /q "%DAEMON_STATUS%" >nul 2>nul
echo Requesting administrator access for the benchmark daemon...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoExit','-NoProfile','-ExecutionPolicy','Bypass','-File','%DAEMON_SCRIPT%','-RepoRoot','%REPO_ROOT%') -WorkingDirectory '%REPO_ROOT%' -Verb RunAs"
if errorlevel 1 (
    echo Administrator approval is required to start the benchmark daemon.
    exit /b 1
)
call :wait_for_daemon_ready 30
exit /b %errorlevel%

:daemon_stop
call :load_daemon_status
if errorlevel 1 (
    echo Benchmark daemon is not running.
    exit /b 1
)
echo Stopping benchmark daemon PID %DAEMON_PID%...
break > "%DAEMON_STOP%"
call :wait_for_daemon_exit 30
exit /b %errorlevel%

:daemon_status
call :load_daemon_status
if errorlevel 1 (
    echo Benchmark daemon is not running.
    exit /b 1
)
echo Benchmark daemon is running with PID %DAEMON_PID%.
exit /b 0

:run_default
call :load_daemon_status
if not errorlevel 1 (
    call :queue_daemon_request
    exit /b %errorlevel%
)
if "%REQUEST_ELEVATION%"=="1" (
    call :relaunch_elevated
    exit /b %errorlevel%
)
echo Benchmark daemon is not running.
echo Start it once with "profile_benchmark.cmd /daemon-start", or rerun this command with /elevate.
exit /b 1

:run_request
if not defined REQUEST_DIR (
    echo Missing request directory for /run-request.
    exit /b 1
)
set "TRACE_ETL=%REQUEST_DIR%\layout_edit_benchmark_wpr.etl"
set "TRACE_TXT=%REQUEST_DIR%\layout_edit_benchmark_wpr.txt"
set "TRACE_CALLTREE_HTML=%REQUEST_DIR%\layout_edit_benchmark_wpr_calltree.html"
set "BENCHMARK_OUTPUT=%REQUEST_DIR%\layout_edit_benchmark_stdout.txt"
del /q "%REQUEST_DIR%\done.env" >nul 2>nul
del /q "%REQUEST_DIR%\error.txt" >nul 2>nul
call :run_benchmark "%TRACE_ETL%" "%TRACE_TXT%" "%TRACE_CALLTREE_HTML%" "%BENCHMARK_OUTPUT%"
set "REQUEST_EXIT_CODE=%errorlevel%"
> "%REQUEST_DIR%\done.env" (
    echo exit_code=%REQUEST_EXIT_CODE%
    echo etl=%TRACE_ETL%
    echo summary=%TRACE_TXT%
    echo calltree=%TRACE_CALLTREE_HTML%
    echo benchmark_output=%BENCHMARK_OUTPUT%
)
exit /b %REQUEST_EXIT_CODE%

:load_daemon_status
set "DAEMON_PID="
if not exist "%DAEMON_STATUS%" exit /b 1
if not exist "%DAEMON_READY%" exit /b 1
for /f "usebackq tokens=1,* delims==" %%A in ("%DAEMON_STATUS%") do (
    if /i "%%A"=="pid" set "DAEMON_PID=%%B"
)
if not defined DAEMON_PID exit /b 1
powershell -NoProfile -Command "if (Get-Process -Id %DAEMON_PID% -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }" >nul 2>nul
if errorlevel 1 exit /b 1
exit /b 0

:wait_for_daemon_ready
setlocal EnableExtensions
set /a WAIT_RETRIES=%~1
:wait_for_daemon_ready_loop
if exist "%DAEMON_READY%" (
    endlocal & exit /b 0
)
if "%WAIT_RETRIES%"=="0" (
    endlocal
    echo Timed out waiting for the benchmark daemon to become ready.
    echo Check the elevated daemon console for startup errors.
    exit /b 1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul
set /a WAIT_RETRIES-=1
goto wait_for_daemon_ready_loop

:wait_for_daemon_exit
setlocal EnableExtensions
set /a WAIT_RETRIES=%~1
:wait_for_daemon_exit_loop
if not exist "%DAEMON_READY%" (
    endlocal & exit /b 0
)
if "%WAIT_RETRIES%"=="0" (
    endlocal
    echo Timed out waiting for the benchmark daemon to stop.
    exit /b 1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul
set /a WAIT_RETRIES-=1
goto wait_for_daemon_exit_loop

:queue_daemon_request
if not exist "%DAEMON_ROOT%\requests" mkdir "%DAEMON_ROOT%\requests"
set "REQUEST_ID=%RANDOM%_%RANDOM%_%RANDOM%"
set "REQUEST_DIR=%DAEMON_ROOT%\requests\%REQUEST_ID%"
mkdir "%REQUEST_DIR%" >nul 2>nul
if errorlevel 1 (
    echo Failed to create benchmark request directory "%REQUEST_DIR%".
    exit /b 1
)
> "%REQUEST_DIR%\request.env" (
    echo iterations=%ITERATIONS%
    echo render_scale=%RENDER_SCALE%
)
echo Waiting for benchmark daemon request %REQUEST_ID%...
call :wait_for_request_completion "%REQUEST_DIR%" 600
call :read_request_exit_code "%REQUEST_DIR%\done.env"
exit /b %REQUEST_RESULT_CODE%

:wait_for_request_completion
setlocal EnableExtensions EnableDelayedExpansion
set "REQUEST_WAIT_DIR=%~1"
set /a WAIT_RETRIES=%~2
:wait_for_request_completion_loop
if exist "%REQUEST_WAIT_DIR%\done.env" (
    set "REQUEST_EXIT_CODE="
    set "REQUEST_ETL="
    set "REQUEST_SUMMARY="
    set "REQUEST_CALLTREE="
    set "REQUEST_BENCHMARK_OUTPUT="
    for /f "usebackq tokens=1,* delims==" %%A in ("%REQUEST_WAIT_DIR%\done.env") do (
        if /i "%%A"=="exit_code" set "REQUEST_EXIT_CODE=%%B"
        if /i "%%A"=="etl" set "REQUEST_ETL=%%B"
        if /i "%%A"=="summary" set "REQUEST_SUMMARY=%%B"
        if /i "%%A"=="calltree" set "REQUEST_CALLTREE=%%B"
        if /i "%%A"=="benchmark_output" set "REQUEST_BENCHMARK_OUTPUT=%%B"
    )
    echo Done.
    if defined REQUEST_BENCHMARK_OUTPUT if exist "!REQUEST_BENCHMARK_OUTPUT!" (
        type "!REQUEST_BENCHMARK_OUTPUT!"
    )
    if defined REQUEST_ETL echo ETL: !REQUEST_ETL!
    if defined REQUEST_SUMMARY echo CPU summary: !REQUEST_SUMMARY!
    if defined REQUEST_CALLTREE echo Call tree: !REQUEST_CALLTREE!
    if not defined REQUEST_EXIT_CODE set "REQUEST_EXIT_CODE=1"
    endlocal & exit /b !REQUEST_EXIT_CODE!
)
if exist "%REQUEST_WAIT_DIR%\error.txt" (
    type "%REQUEST_WAIT_DIR%\error.txt"
    endlocal & exit /b 1
)
if "%WAIT_RETRIES%"=="0" (
    echo Timed out waiting for the benchmark daemon to finish the request.
    endlocal & exit /b 1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul
set /a WAIT_RETRIES-=1
goto wait_for_request_completion_loop

:read_request_exit_code
setlocal EnableExtensions
set "REQUEST_DONE_FILE=%~1"
set /a REQUEST_RESULT_CODE=1
if exist "%REQUEST_DONE_FILE%" (
    for /f "usebackq tokens=1,* delims==" %%A in ("%REQUEST_DONE_FILE%") do (
        if /i "%%A"=="exit_code" set /a REQUEST_RESULT_CODE=%%B+0
    )
)
endlocal & set "REQUEST_RESULT_CODE=%REQUEST_RESULT_CODE%"
exit /b 0

:run_benchmark
set "TRACE_ETL=%~1"
set "TRACE_TXT=%~2"
set "TRACE_CALLTREE_HTML=%~3"
set "BENCHMARK_OUTPUT=%~4"
set "WPR_EXE=%SystemRoot%\System32\wpr.exe"
set "XPERF_EXE=C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe"
set "BENCHMARK_EXE=%REPO_ROOT%\build\SystemTelemetryBenchmarks.exe"
set "BENCHMARK_PDB_DIR=%REPO_ROOT%\build\cmake\CMakeFiles\SystemTelemetryBenchmarks.dir\Release"
set "FORCE_BUILD=%PROFILE_BENCHMARK_FORCE_BUILD%"

if not exist "%WPR_EXE%" (
    echo Windows Performance Recorder was not found at "%WPR_EXE%".
    exit /b 1
)

if not exist "%XPERF_EXE%" (
    echo xperf.exe was not found at "%XPERF_EXE%".
    echo Install the Windows Performance Toolkit, then rerun this script.
    exit /b 1
)

whoami /priv | findstr /c:"SeSystemProfilePrivilege" >nul
if errorlevel 1 (
    echo This script requires an elevated Administrator process with SeSystemProfilePrivilege.
    exit /b 1
)

if /i "%FORCE_BUILD%"=="1" (
    echo Building Release benchmark binaries before profiling...
    call "%REPO_ROOT%\build.cmd" Release
    if errorlevel 1 exit /b %errorlevel%
) else if not exist "%BENCHMARK_EXE%" (
    call "%REPO_ROOT%\build.cmd" Release
    if errorlevel 1 exit /b %errorlevel%
)

set "_NT_SYMBOL_PATH=%REPO_ROOT%\build;%BENCHMARK_PDB_DIR%"

del /q "%TRACE_ETL%" >nul 2>nul
del /q "%TRACE_TXT%" >nul 2>nul
del /q "%TRACE_CALLTREE_HTML%" >nul 2>nul
if defined BENCHMARK_OUTPUT del /q "%BENCHMARK_OUTPUT%" >nul 2>nul

echo Starting CPU profile capture...
"%WPR_EXE%" -cancel >nul 2>nul
"%WPR_EXE%" -start CPU.verbose -filemode
if errorlevel 1 (
    echo Failed to start WPR CPU profiling.
    exit /b %errorlevel%
)

echo Running benchmark iterations=%ITERATIONS% scale=%RENDER_SCALE%...
if defined BENCHMARK_OUTPUT (
    set "PROFILE_BENCHMARK_STDOUT=%BENCHMARK_OUTPUT%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& { & $env:BENCHMARK_EXE %ITERATIONS% %RENDER_SCALE% 2>&1 | Tee-Object -FilePath $env:PROFILE_BENCHMARK_STDOUT; exit $LASTEXITCODE }"
) else (
    "%BENCHMARK_EXE%" %ITERATIONS% %RENDER_SCALE%
)
set "BENCHMARK_RC=%errorlevel%"

echo Stopping trace and writing "%TRACE_ETL%"...
"%WPR_EXE%" -stop "%TRACE_ETL%"
if errorlevel 1 (
    echo Failed to stop WPR and save the ETL trace.
    exit /b %errorlevel%
)

echo Exporting CPU profile summary to "%TRACE_TXT%"...
"%XPERF_EXE%" -i "%TRACE_ETL%" -symbols -a profile -detail > "%TRACE_TXT%"
if errorlevel 1 (
    echo Failed to export the CPU profile summary.
    exit /b %errorlevel%
)

echo Exporting benchmark call tree to "%TRACE_CALLTREE_HTML%"...
"%XPERF_EXE%" -i "%TRACE_ETL%" -symbols -a stack -process "SystemTelemetryBenchmarks.exe" -butterfly 1 > "%TRACE_CALLTREE_HTML%"
if errorlevel 1 (
    echo Failed to export the benchmark call tree.
    exit /b %errorlevel%
)
exit /b %BENCHMARK_RC%

:relaunch_elevated
if "%IS_ELEVATED_RELAUNCH%"=="1" (
    echo Failed to acquire SeSystemProfilePrivilege after elevation.
    exit /b 1
)

if not exist "%DAEMON_ROOT%\adhoc" mkdir "%DAEMON_ROOT%\adhoc"
del /q "%DAEMON_ROOT%\adhoc\done.env" >nul 2>nul
del /q "%DAEMON_ROOT%\adhoc\error.txt" >nul 2>nul
del /q "%DAEMON_ROOT%\adhoc\layout_edit_benchmark_stdout.txt" >nul 2>nul
del /q "%DAEMON_ROOT%\adhoc\layout_edit_benchmark_wpr.etl" >nul 2>nul
del /q "%DAEMON_ROOT%\adhoc\layout_edit_benchmark_wpr.txt" >nul 2>nul
del /q "%DAEMON_ROOT%\adhoc\layout_edit_benchmark_wpr_calltree.html" >nul 2>nul
echo Requesting administrator access for WPR CPU profiling...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath $env:ComSpec -ArgumentList '/c ""%~f0"" %ITERATIONS% %RENDER_SCALE% /elevated /run-request ""%DAEMON_ROOT%\adhoc""' -WorkingDirectory '%REPO_ROOT%' -Verb RunAs -Wait | Out-Null"
if errorlevel 1 (
    echo Administrator approval is required to capture the benchmark CPU profile.
    exit /b 1
)
call :wait_for_request_completion "%DAEMON_ROOT%\adhoc" 10
call :read_request_exit_code "%DAEMON_ROOT%\adhoc\done.env"
exit /b %REQUEST_RESULT_CODE%
