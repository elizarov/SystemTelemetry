@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "script_root=%~dp0"
set "root_arg=%script_root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%script_root%" >nul || exit /b 1

set "mode=check"
set "scope=all"
set "target_file="
set "target_path="
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
if /I "%~1"=="--path" (
    if "%~2"=="" goto :usage
    set "target_path=%~2"
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
if defined target_path if /I "!scope!"=="changed" goto :usage
if defined target_file if defined target_path goto :usage
if "!stdout!"=="1" if /I "!mode!"=="fix" goto :usage
if "!stdout!"=="1" if defined target_path goto :usage

call :ensure_format_tool
set "ensure_format_tool_result=!errorlevel!"
if not "!ensure_format_tool_result!"=="0" (
    popd >nul
    exit /b !ensure_format_tool_result!
)

set "mode_arg="
set "scope_arg="
if /I "!mode!"=="fix" set "mode_arg=fix"
if /I "!scope!"=="changed" set "scope_arg=changed"

if defined target_file (
    if "!stdout!"=="1" (
        "%script_root%build\CaseDashTools.exe" format !mode_arg! --root "%root_arg%" --file "%target_file%" --stdout
    ) else (
        "%script_root%build\CaseDashTools.exe" format !mode_arg! --root "%root_arg%" --file "%target_file%"
    )
) else if defined target_path (
    "%script_root%build\CaseDashTools.exe" format !mode_arg! --root "%root_arg%" --path "%target_path%"
) else (
    "%script_root%build\CaseDashTools.exe" format !mode_arg! !scope_arg! --root "%root_arg%"
)
set "result=%errorlevel%"
popd >nul
exit /b %result%

:ensure_format_tool
if exist "%script_root%build\CaseDashTools.exe" exit /b 0
call "%script_root%build.cmd" /tools
exit /b !errorlevel!

:usage
echo Usage:
echo   format
echo   format fix
echo   format changed
echo   format fix changed
echo   format [fix] --path file-or-directory
echo   format [--root path] --file path [--stdout]
popd >nul
exit /b 2
