@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "root=%~dp0"
set "root_arg=%root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%root%" >nul || exit /b 1

set "failed=0"
set "lint_check_args=--check --skip-svg"
set "include_lint=0"
set "include_scope=all"

:parse_lint_args
if "%~1"=="" goto lint_args_done
if /I "%~1"=="-v" (
    set "lint_check_args=!lint_check_args! --verbose"
    shift
    goto parse_lint_args
)
if /I "%~1"=="--verbose" (
    set "lint_check_args=!lint_check_args! --verbose"
    shift
    goto parse_lint_args
)
if /I "%~1"=="includes" (
    if "!include_lint!"=="1" goto :usage
    set "include_lint=1"
    shift
    goto parse_lint_args
)
if /I "%~1"=="changed" (
    if not "!include_lint!"=="1" goto :usage
    if /I "!include_scope!"=="changed" goto :usage
    set "include_scope=changed"
    shift
    goto parse_lint_args
)
goto :usage

:lint_args_done

python tools\lint_check.py !lint_check_args!
if errorlevel 1 set "failed=1"

if "!include_lint!"=="1" (
    if /I "!include_scope!"=="changed" (
        call :run_include_lint changed
    ) else (
        call :run_include_lint
    )
    set "include_lint_result=!errorlevel!"
    if "!include_lint_result!"=="2" (
        popd >nul
        exit /b 2
    )
    if not "!include_lint_result!"=="0" set "failed=1"
)

if "%failed%"=="0" (
    popd >nul
    exit /b 0
)

echo Lint failed.
popd >nul
exit /b 1

:run_include_lint
set "scope=all"

:parse_include_lint_args
if "%~1"=="" goto include_lint_args_done
if /I "%~1"=="changed" (
    set "scope=changed"
    shift
    goto parse_include_lint_args
)
goto :usage

:include_lint_args_done

set "include_lint_ps_args="
if defined CASEDASH_INCLUDE_LINT_MAX_PARALLEL set "include_lint_ps_args=!include_lint_ps_args! -MaxParallel !CASEDASH_INCLUDE_LINT_MAX_PARALLEL!"
if defined CASEDASH_INCLUDE_LINT_TIMEOUT_SECONDS set "include_lint_ps_args=!include_lint_ps_args! -TimeoutSeconds !CASEDASH_INCLUDE_LINT_TIMEOUT_SECONDS!"
powershell -NoProfile -ExecutionPolicy Bypass -File "%root%tools\run_clangd_includes.ps1" -Root "%root_arg%" -Scope "%scope%" !include_lint_ps_args!
exit /b !errorlevel!

:usage
echo Usage:
echo   lint [-v^|--verbose]
echo   lint [-v^|--verbose] includes
echo   lint [-v^|--verbose] includes changed
popd >nul
exit /b 2
