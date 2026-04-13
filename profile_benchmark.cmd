@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

call "%REPO_ROOT%\devenv.cmd"
if errorlevel 1 exit /b %errorlevel%

if "%~1"=="" (
    set "ITERATIONS=600"
) else (
    set "ITERATIONS=%~1"
)

if "%~2"=="" (
    set "RENDER_SCALE=2"
) else (
    set "RENDER_SCALE=%~2"
)

set "WPR_EXE=%SystemRoot%\System32\wpr.exe"
set "XPERF_EXE=C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe"
set "BENCHMARK_EXE=%REPO_ROOT%\build\SystemTelemetryBenchmarks.exe"
set "BENCHMARK_PDB_DIR=%REPO_ROOT%\build\cmake\CMakeFiles\SystemTelemetryBenchmarks.dir\Release"
set "TRACE_ETL=%REPO_ROOT%\build\layout_edit_benchmark_wpr.etl"
set "TRACE_TXT=%REPO_ROOT%\build\layout_edit_benchmark_wpr.txt"
set "TRACE_CALLTREE_HTML=%REPO_ROOT%\build\layout_edit_benchmark_wpr_calltree.html"

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
    echo This script requires a real elevated Administrator shell with SeSystemProfilePrivilege.
    echo Start an Administrator terminal, then rerun profile_benchmark.cmd.
    exit /b 1
)

if not exist "%BENCHMARK_EXE%" (
    call "%REPO_ROOT%\build.cmd" Release
    if errorlevel 1 exit /b %errorlevel%
)

set "_NT_SYMBOL_PATH=%REPO_ROOT%\build;%BENCHMARK_PDB_DIR%"

del /q "%TRACE_ETL%" >nul 2>nul
del /q "%TRACE_TXT%" >nul 2>nul
del /q "%TRACE_CALLTREE_HTML%" >nul 2>nul

echo Starting CPU profile capture...
"%WPR_EXE%" -cancel >nul 2>nul
"%WPR_EXE%" -start CPU.verbose -filemode
if errorlevel 1 (
    echo Failed to start WPR CPU profiling.
    exit /b %errorlevel%
)

echo Running benchmark iterations=%ITERATIONS% scale=%RENDER_SCALE%...
"%BENCHMARK_EXE%" %ITERATIONS% %RENDER_SCALE%
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

echo Done.
echo ETL: %TRACE_ETL%
echo CPU summary: %TRACE_TXT%
echo Call tree: %TRACE_CALLTREE_HTML%
exit /b %BENCHMARK_RC%
