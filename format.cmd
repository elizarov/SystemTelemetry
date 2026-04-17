@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "root=%~dp0"
set "root_arg=%root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%root%" >nul || exit /b 1

set "mode=check"
set "scope=all"

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
goto :usage

:args_done

powershell -NoProfile -ExecutionPolicy Bypass -File "%root%tools\run_clang_format.ps1" -Root "%root_arg%" -Mode "%mode%" -Scope "%scope%"
set "result=%errorlevel%"
popd >nul
exit /b %result%

:usage
echo Usage:
echo   format
echo   format fix
echo   format changed
echo   format fix changed
popd >nul
exit /b 2
