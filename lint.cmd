@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "root=%~dp0"
set "root_arg=%root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%root%" >nul || exit /b 1

set "failed=0"
echo Running architecture checks...
python tools\check_architecture.py
if errorlevel 1 set "failed=1"

echo Running source dependency checks...
python tools\source_dependency_graph.py --check
if errorlevel 1 set "failed=1"

echo Running include style checks...
python tools\check_includes.py
if errorlevel 1 set "failed=1"

if /I "%~1"=="tidy" (
    call :run_clang_tidy %~2 %~3
    set "tidy_result=%errorlevel%"
    if "%tidy_result%"=="2" (
        popd >nul
        exit /b 2
    )
    if not "%tidy_result%"=="0" set "failed=1"
) else if not "%~1"=="" (
    goto :usage
)

if "%failed%"=="0" (
    echo Lint passed.
    popd >nul
    exit /b 0
)

echo Lint failed.
popd >nul
exit /b 1

:run_clang_tidy
set "mode=check"
set "scope=all"

:parse_clang_tidy_args
if "%~1"=="" goto clang_tidy_args_done
if /I "%~1"=="fix" (
    set "mode=fix"
    shift
    goto parse_clang_tidy_args
)
if /I "%~1"=="changed" (
    set "scope=changed"
    shift
    goto parse_clang_tidy_args
)
goto :usage

:clang_tidy_args_done

powershell -NoProfile -ExecutionPolicy Bypass -File "%root%tools\run_clang_tidy.ps1" -Root "%root_arg%" -Mode "%mode%" -Scope "%scope%"
exit /b %errorlevel%

:usage
echo Usage:
echo   lint
echo   lint tidy
echo   lint tidy fix
echo   lint tidy changed
echo   lint tidy changed fix
popd >nul
exit /b 2
