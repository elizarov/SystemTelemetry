@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

if /I "%GITHUB_ACTIONS%"=="true" (
    call :github_devenv
) else (
    call "%REPO_ROOT%\devenv.cmd"
)
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir "%REPO_ROOT%\build\cmake" -C Release --verbose --output-on-failure
exit /b %errorlevel%

:github_devenv
if defined VSCMD_ARG_TGT_ARCH exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
    for /f "delims=" %%I in ('call "%VSWHERE%" -latest -products * -version "[18.0,19.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do (
        if not defined VSINSTALL set "VSINSTALL=%%I"
    )
    if not defined VSINSTALL (
        for /f "delims=" %%I in ('call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do (
            if not defined VSINSTALL set "VSINSTALL=%%I"
        )
    )
)

if not defined VSINSTALL if defined VSINSTALLDIR set "VSINSTALL=%VSINSTALLDIR%"
if not defined VSINSTALL (
    echo Visual Studio C++ tools were not found on the GitHub runner.
    exit /b 1
)

set "VSDEV=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"
if exist "%VSDEV%" (
    call "%VSDEV%" -arch=x64 -host_arch=x64
    exit /b %errorlevel%
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" (
    call "%VCVARS%"
    exit /b %errorlevel%
)

echo Visual Studio developer environment bootstrap was not found under "%VSINSTALL%".
exit /b 1
