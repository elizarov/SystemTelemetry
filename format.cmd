@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "root=%~dp0"
pushd "%root%" >nul || exit /b 1

call :resolve_clang_format
if errorlevel 1 (
    echo clang-format.exe was not found. Install the Visual Studio LLVM tools or add clang-format to PATH.
    popd >nul
    exit /b 1
)

set "mode=check"
if /I "%~1"=="fix" set "mode=fix"
if not "%~1"=="" if /I not "%~1"=="fix" goto :usage
if not "%~2"=="" goto :usage

set "file_count=0"
set "failed=0"

for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$files = git -c core.quotepath=off ls-files -- '*.cpp' '*.h'; $files | Where-Object { $_ -notlike 'src/vendor/*' }"`) do (
    set /a file_count+=1
    if /I "%mode%"=="fix" (
        "%clang_format%" -i "%%F"
    ) else (
        "%clang_format%" --dry-run --Werror "%%F"
    )
    if errorlevel 1 set "failed=1"
)

if "%file_count%"=="0" (
    echo No non-vendored C++ source files were found.
    popd >nul
    exit /b 1
)

if /I "%mode%"=="fix" (
    if "%failed%"=="0" (
        echo Formatted %file_count% files.
        popd >nul
        exit /b 0
    )
    echo Formatting failed.
    popd >nul
    exit /b 1
)

if "%failed%"=="0" (
    echo Checked %file_count% files. Formatting is up to date.
    popd >nul
    exit /b 0
)

echo Formatting is required. Run "format fix".
popd >nul
exit /b 1

:resolve_clang_format
set "clang_format="
for %%P in (
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\bin\clang-format.exe"
    "%VSINSTALLDIR%VC\Tools\Llvm\x64\bin\clang-format.exe"
    "%VSINSTALLDIR%VC\Tools\Llvm\bin\clang-format.exe"
) do (
    if exist %%~P (
        set "clang_format=%%~fP"
        exit /b 0
    )
)

for /f "usebackq delims=" %%P in (`where clang-format.exe 2^>nul`) do (
    set "clang_format=%%~fP"
    exit /b 0
)

if exist "%root%devenv.cmd" (
    call "%root%devenv.cmd" >nul 2>&1
    for /f "usebackq delims=" %%P in (`where clang-format.exe 2^>nul`) do (
        set "clang_format=%%~fP"
        exit /b 0
    )
)

exit /b 1

:usage
echo Usage:
echo   format
echo   format fix
popd >nul
exit /b 2
