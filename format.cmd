@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "script_root=%~dp0"
set "root_arg=%script_root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%script_root%" >nul || exit /b 1

set "mode=check"
set "scope=all"
set "target_file="
set "stdout=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="fix" (
    set "mode=fix"
    shift
    goto parse_args
)
if /I "%~1"=="changed" (
    set "scope=changed"
    shift
    goto parse_args
)
if /I "%~1"=="--root" (
    if "%~2"=="" goto :usage
    set "root_arg=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--file" (
    if "%~2"=="" goto :usage
    set "target_file=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--stdout" (
    set "stdout=1"
    shift
    goto parse_args
)
goto :usage

:args_done

if "!stdout!"=="1" if not defined target_file goto :usage
if defined target_file if /I "!scope!"=="changed" goto :usage
if "!stdout!"=="1" if /I "!mode!"=="fix" goto :usage

if defined target_file (
    if "!stdout!"=="1" (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%script_root%tools\run_clang_format.ps1" -Root "%root_arg%" -Mode "%mode%" -Scope "%scope%" -TargetFile "%target_file%" -Stdout
    ) else (
        powershell -NoProfile -ExecutionPolicy Bypass -File "%script_root%tools\run_clang_format.ps1" -Root "%root_arg%" -Mode "%mode%" -Scope "%scope%" -TargetFile "%target_file%"
    )
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%script_root%tools\run_clang_format.ps1" -Root "%root_arg%" -Mode "%mode%" -Scope "%scope%"
)
set "result=%errorlevel%"
popd >nul
exit /b %result%

:usage
echo Usage:
echo   format
echo   format fix
echo   format changed
echo   format fix changed
echo   format [--root path] --file path [--stdout]
popd >nul
exit /b 2
