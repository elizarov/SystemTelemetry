@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "root=%~dp0"
pushd "%root%" >nul || exit /b 1

set "failed=0"
echo Running architecture checks...
python tools\check_architecture.py
if errorlevel 1 set "failed=1"

echo Running include style checks...
python tools\check_includes.py
if errorlevel 1 set "failed=1"

if /I "%~1"=="tidy" (
    shift
    call :run_clang_tidy %*
    if errorlevel 1 set "failed=1"
) else if not "%~1"=="" (
    goto :usage
)

if "%failed%"=="0" (
    if /I "%~1"=="tidy" (
        echo Lint passed.
    ) else (
        echo Lint passed.
    )
    popd >nul
    exit /b 0
)

echo Lint failed.
popd >nul
exit /b 1

:run_clang_tidy
set "mode=check"
if /I "%~1"=="fix" set "mode=fix"
if not "%~1"=="" if /I not "%~1"=="fix" goto :usage
if not "%~2"=="" goto :usage

call :resolve_clang_tidy
if errorlevel 1 (
    echo clang-tidy.exe was not found. Install the Visual Studio LLVM tools or add clang-tidy to PATH.
    exit /b 1
)

if not exist "%root%build\cmake\compile_commands.json" (
    echo build\cmake\compile_commands.json was not found. Build the project first with build.cmd.
    exit /b 1
)

set "file_count=0"
set "file_list=%root%build\lint_cpp_files.txt"

echo Using clang-tidy: %clang_tidy%
echo Preparing lint file list...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$files = git -c core.quotepath=off ls-files -- '*.cpp'; $files | Where-Object { $_ -notlike 'src/vendor/*' -and $_ -ne 'src/board_gigabyte_siv.cpp' } | Set-Content -Encoding utf8 '%file_list%'"
if errorlevel 1 (
    echo Failed to prepare the lint file list.
    exit /b 1
)

for /f %%N in ('find /c /v "" ^< "%file_list%"') do set "file_count=%%N"
if "%file_count%"=="0" (
    echo No eligible project translation units were found.
    if exist "%file_list%" del /q "%file_list%" >nul 2>&1
    exit /b 1
)

echo Running optional clang-tidy sweep for %file_count% translation units...
set "current_index=0"
set "tidy_failed=0"
for /f "usebackq delims=" %%F in ("%file_list%") do (
    set /a current_index+=1
    echo [!current_index!/%file_count%] %%F
    if /I "%mode%"=="fix" (
        "%clang_tidy%" -p "%root%build\cmake" --quiet -fix --format-style=none "%%F"
    ) else (
        "%clang_tidy%" -p "%root%build\cmake" --quiet "%%F"
    )
    if errorlevel 1 set "tidy_failed=1"
)

if exist "%file_list%" del /q "%file_list%" >nul 2>&1

if "%tidy_failed%"=="0" (
    if /I "%mode%"=="fix" (
        echo clang-tidy fixes completed.
    ) else (
        echo clang-tidy passed.
    )
    exit /b 0
)

if /I "%mode%"=="fix" (
    echo clang-tidy fix completed with remaining issues.
) else (
    echo clang-tidy failed.
)
exit /b 1

:resolve_clang_tidy
set "clang_tidy="
for %%P in (
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\bin\clang-tidy.exe"
    "%VSINSTALLDIR%VC\Tools\Llvm\x64\bin\clang-tidy.exe"
    "%VSINSTALLDIR%VC\Tools\Llvm\bin\clang-tidy.exe"
) do (
    if exist %%~P (
        set "clang_tidy=%%~fP"
        exit /b 0
    )
)

for /f "usebackq delims=" %%P in (`where clang-tidy.exe 2^>nul`) do (
    set "clang_tidy=%%~fP"
    exit /b 0
)

if exist "%root%devenv.cmd" (
    call "%root%devenv.cmd" >nul 2>&1
    for /f "usebackq delims=" %%P in (`where clang-tidy.exe 2^>nul`) do (
        set "clang_tidy=%%~fP"
        exit /b 0
    )
)

exit /b 1

:usage
echo Usage:
echo   lint
echo   lint tidy
echo   lint tidy fix
popd >nul
exit /b 2
