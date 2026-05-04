@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

set "BUILD_ARGS=%*"
set "BUILD_BENCHMARKS=0"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="/benchmarks" set "BUILD_BENCHMARKS=1"
if /I "%~1"=="/benchmark" set "BUILD_BENCHMARKS=1"
shift
goto parse_args

:args_done
set "CASEDASH_LINK_MAPS=ON"
del /q "%REPO_ROOT%\build\CaseDash.map" "%REPO_ROOT%\build\CaseDash.map.summary.txt" >nul 2>nul
del /q "%REPO_ROOT%\build\CaseDashBenchmarks.map" "%REPO_ROOT%\build\CaseDashBenchmarks.map.summary.txt" >nul 2>nul
del /q "%REPO_ROOT%\build\CaseDash.exe" >nul 2>nul
if "%BUILD_BENCHMARKS%"=="1" (
    del /q "%REPO_ROOT%\build\CaseDashBenchmarks.exe" >nul 2>nul
)
call "%REPO_ROOT%\build.cmd" %BUILD_ARGS%
if errorlevel 1 exit /b %errorlevel%

python "%REPO_ROOT%\tools\analyze_link_map.py" "%REPO_ROOT%\build\CaseDash.map" --top 25 > "%REPO_ROOT%\build\CaseDash.map.summary.txt"
if errorlevel 1 exit /b %errorlevel%

if "%BUILD_BENCHMARKS%"=="1" (
    python "%REPO_ROOT%\tools\analyze_link_map.py" "%REPO_ROOT%\build\CaseDashBenchmarks.map" --top 15 > "%REPO_ROOT%\build\CaseDashBenchmarks.map.summary.txt"
    if errorlevel 1 exit /b %errorlevel%
)

echo Map summaries written:
echo   build\CaseDash.map.summary.txt
if "%BUILD_BENCHMARKS%"=="1" echo   build\CaseDashBenchmarks.map.summary.txt
exit /b %errorlevel%
