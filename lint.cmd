@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "root=%~dp0"
set "root_arg=%root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%root%" >nul || exit /b 1

set "failed=0"
echo Running combined lint checks...
python tools\lint_check.py --check --skip-svg
if errorlevel 1 set "failed=1"

if /I "%~1"=="includes" (
    call :run_include_lint %~2 %~3
    set "include_lint_result=!errorlevel!"
    if "!include_lint_result!"=="2" (
        popd >nul
        exit /b 2
    )
    if not "!include_lint_result!"=="0" set "failed=1"
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
echo   lint
echo   lint includes
echo   lint includes changed
popd >nul
exit /b 2
