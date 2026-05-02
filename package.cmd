@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

call "%REPO_ROOT%\build.cmd"
if errorlevel 1 exit /b %errorlevel%

if /I "%GITHUB_ACTIONS%"=="true" (
    call :github_devenv
) else (
    call "%REPO_ROOT%\devenv.cmd"
)
if errorlevel 1 exit /b %errorlevel%

for /f "usebackq delims=" %%V in ("%REPO_ROOT%\VERSION") do (
    if not defined CASEDASH_VERSION_TEXT set "CASEDASH_VERSION_TEXT=%%V"
)
if not defined CASEDASH_VERSION_TEXT (
    echo VERSION is empty.
    exit /b 1
)

set "CASEDASH_MSI_VERSION=%CASEDASH_VERSION_TEXT%"
for /f "tokens=1-4 delims=." %%A in ("%CASEDASH_VERSION_TEXT%") do (
    set "VERSION_MAJOR=%%A"
    set "VERSION_MINOR=%%B"
    set "VERSION_PATCH=%%C"
    set "VERSION_EXTRA=%%D"
)
if defined VERSION_EXTRA (
    echo VERSION must use major.minor or major.minor.patch format.
    exit /b 1
)
if not defined VERSION_MAJOR (
    echo VERSION must use major.minor or major.minor.patch format.
    exit /b 1
)
if not defined VERSION_MINOR (
    echo VERSION must use major.minor or major.minor.patch format.
    exit /b 1
)
echo %VERSION_MAJOR%| findstr /R "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo VERSION must use major.minor or major.minor.patch format.
    exit /b 1
)
echo %VERSION_MINOR%| findstr /R "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo VERSION must use major.minor or major.minor.patch format.
    exit /b 1
)
if defined VERSION_PATCH (
    echo %VERSION_PATCH%| findstr /R "^[0-9][0-9]*$" >nul
    if errorlevel 1 (
        echo VERSION must use major.minor or major.minor.patch format.
        exit /b 1
    )
)
if not defined VERSION_PATCH set "CASEDASH_MSI_VERSION=%VERSION_MAJOR%.%VERSION_MINOR%.0"

set "SOURCE_EXE=%REPO_ROOT%\build\CaseDash.exe"
set "OUTPUT_MSI=%REPO_ROOT%\build\CaseDash-%CASEDASH_VERSION_TEXT%.msi"
set "OUTPUT_SHA=%OUTPUT_MSI%.sha256"
set "MSBUILD_REPO_ROOT=%REPO_ROOT:\=/%"
set "MSBUILD_SOURCE_EXE=%MSBUILD_REPO_ROOT%/build/CaseDash.exe"

if not exist "%SOURCE_EXE%" (
    echo Missing build output: "%SOURCE_EXE%"
    exit /b 1
)

where msbuild >nul 2>nul
if errorlevel 1 (
    echo MSBuild was not found after developer environment setup.
    exit /b 1
)

msbuild "%REPO_ROOT%\installer\CaseDash.Installer.wixproj" /restore /p:Configuration=Release /p:Platform=x64 /p:CaseDashVersionText="%CASEDASH_VERSION_TEXT%" /p:ProductVersion="%CASEDASH_MSI_VERSION%" /p:CaseDashExePath="%MSBUILD_SOURCE_EXE%" /p:OutputPath="%MSBUILD_REPO_ROOT%/build/" /p:BaseIntermediateOutputPath="%MSBUILD_REPO_ROOT%/build/installer/obj/" /p:IntermediateOutputPath="%MSBUILD_REPO_ROOT%/build/installer/"
if errorlevel 1 exit /b %errorlevel%

if not exist "%OUTPUT_MSI%" (
    echo Missing MSI output: "%OUTPUT_MSI%"
    exit /b 1
)

set "CASEDASH_OUTPUT_MSI=%OUTPUT_MSI%"
set "CASEDASH_OUTPUT_NAME=CaseDash-%CASEDASH_VERSION_TEXT%.msi"
set "CASEDASH_OUTPUT_SHA=%OUTPUT_SHA%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$hash = Get-FileHash -LiteralPath $env:CASEDASH_OUTPUT_MSI -Algorithm SHA256; ('{0}  {1}' -f $hash.Hash, $env:CASEDASH_OUTPUT_NAME) | Set-Content -LiteralPath $env:CASEDASH_OUTPUT_SHA"
if errorlevel 1 exit /b %errorlevel%

echo MSI package: "%OUTPUT_MSI%"
echo MSI checksum: "%OUTPUT_SHA%"
exit /b 0

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
