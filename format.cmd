@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "script_root=%~dp0"
set "root_arg=%script_root%"
if "%root_arg:~-1%"=="\" set "root_arg=%root_arg:~0,-1%"
pushd "%script_root%" >nul || exit /b 1

set "mode=check"
set "scope=all"
set "restage=0"
set "verbose=0"
set "concurrency_arg="

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
if /I "%~1"=="change" (
    set "scope=changed"
    shift
    goto parse_args
)
if /I "%~1"=="staged" (
    set "scope=staged"
    shift
    goto parse_args
)
if /I "%~1"=="--restage" (
    set "restage=1"
    shift
    goto parse_args
)
if /I "%~1"=="-v" (
    set "verbose=1"
    shift
    goto parse_args
)
if /I "%~1"=="--verbose" (
    set "verbose=1"
    shift
    goto parse_args
)
if /I "%~1"=="--concurrency" (
    if "%~2"=="" goto :usage
    set "concurrency_arg=--concurrency %~2"
    shift
    shift
    goto parse_args
)
set "arg=%~1"
if /I "!arg:~0,14!"=="--concurrency=" (
    if "!arg:~14!"=="" goto :usage
    set "concurrency_arg=--concurrency !arg:~14!"
    shift
    goto parse_args
)
goto :usage

:args_done

if "!restage!"=="1" if /I not "!mode!"=="fix" goto :usage
if "!restage!"=="1" if /I not "!scope!"=="staged" goto :usage

call :ensure_format_tool
set "ensure_format_tool_result=!errorlevel!"
if not "!ensure_format_tool_result!"=="0" (
    popd >nul
    exit /b !ensure_format_tool_result!
)

if not exist "%script_root%build" mkdir "%script_root%build"
set "candidate_list=%script_root%build\format_candidates_%RANDOM%_%RANDOM%.txt"
set "file_list=%script_root%build\format_files_%RANDOM%_%RANDOM%.txt"
if exist "!candidate_list!" del /q "!candidate_list!" >nul 2>nul
if exist "!file_list!" del /q "!file_list!" >nul 2>nul

call :collect_files
set "collect_result=!errorlevel!"
if not "!collect_result!"=="0" (
    del /q "!candidate_list!" "!file_list!" >nul 2>nul
    popd >nul
    exit /b !collect_result!
)

for %%I in ("!file_list!") do set "file_list_size=%%~zI"
if "!file_list_size!"=="0" (
    if /I "!scope!"=="all" (
        echo No maintained C++ source files were found.
        set "empty_result=1"
    ) else (
        echo No eligible !scope! C++ source files were found.
        set "empty_result=0"
    )
    del /q "!candidate_list!" "!file_list!" >nul 2>nul
    popd >nul
    exit /b !empty_result!
)

set "native_options=--style=file"
if /I "!mode!"=="fix" (
    set "native_options=!native_options! -i"
) else (
    set "native_options=!native_options! --dry-run"
)
if defined concurrency_arg set "native_options=!native_options! !concurrency_arg!"
if "!verbose!"=="1" set "native_options=!native_options! --verbose"

set "format_failed=0"
"%script_root%build\CaseDashTools.exe" format !native_options! --files "!file_list!"
set "format_failed=!errorlevel!"

if "!format_failed!"=="0" if "!restage!"=="1" (
    set "chunk_args="
    set "chunk_count=0"
    for /f "usebackq delims=" %%F in ("!file_list!") do (
        call :append_git_file "%%F"
    )
    call :run_git_add_chunk
)

del /q "!candidate_list!" "!file_list!" >nul 2>nul
popd >nul
exit /b !format_failed!

:collect_files
if /I "!scope!"=="all" (
    git -C "%root_arg%" -c core.quotepath=off -c core.safecrlf=false ls-files --cached --others --exclude-standard -- src tests > "!candidate_list!"
) else if /I "!scope!"=="changed" (
    git -C "%root_arg%" rev-parse --verify HEAD >nul 2>nul
    if errorlevel 1 (
        git -C "%root_arg%" -c core.quotepath=off -c core.safecrlf=false diff --name-only --diff-filter=ACMR --cached -- src tests > "!candidate_list!"
    ) else (
        git -C "%root_arg%" -c core.quotepath=off -c core.safecrlf=false diff --name-only --diff-filter=ACMR HEAD -- src tests > "!candidate_list!"
    )
    git -C "%root_arg%" -c core.quotepath=off -c core.safecrlf=false ls-files --others --exclude-standard -- src tests >> "!candidate_list!"
) else if /I "!scope!"=="staged" (
    git -C "%root_arg%" -c core.quotepath=off -c core.safecrlf=false diff --cached --name-only --diff-filter=ACMR -- src tests > "!candidate_list!"
) else (
    exit /b 2
)
if errorlevel 1 exit /b !errorlevel!

set "last_file="
> "!file_list!" (
    for /f "usebackq delims=" %%F in (`sort "!candidate_list!"`) do (
        if /I not "%%F"=="!last_file!" (
            set "last_file=%%F"
            if /I "%%~xF"==".cpp" if exist "%root_arg%\%%F" echo %%F
            if /I "%%~xF"==".h" if exist "%root_arg%\%%F" echo %%F
        )
    )
)
exit /b 0

:append_git_file
set arg="%~1"
set "chunk_args=!chunk_args! !arg!"
set /a chunk_count+=1
if !chunk_count! GEQ 80 call :run_git_add_chunk
exit /b 0

:run_git_add_chunk
if "!chunk_count!"=="0" exit /b 0
git -C "%root_arg%" -c core.safecrlf=false add -- !chunk_args!
if errorlevel 1 set "format_failed=1"
set "chunk_args="
set "chunk_count=0"
exit /b 0

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
echo   format fix staged --restage
echo   format [--concurrency n]
popd >nul
exit /b 2
