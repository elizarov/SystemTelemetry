@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

if /I "%GITHUB_ACTIONS%"=="true" (
    call :github_devenv
) else (
    call "%REPO_ROOT%\devenv.cmd"
)
if errorlevel 1 exit /b %errorlevel%

if not exist "%REPO_ROOT%\build" mkdir "%REPO_ROOT%\build"
set "SCRATCH_ROOT=%TEMP%"
if not defined SCRATCH_ROOT set "SCRATCH_ROOT=%TMP%"
if not defined SCRATCH_ROOT if defined LOCALAPPDATA set "SCRATCH_ROOT=%LOCALAPPDATA%\Temp"
if not defined SCRATCH_ROOT set "SCRATCH_ROOT=%REPO_ROOT%\build\tmp"
set "SCRATCH_ROOT=%SCRATCH_ROOT%\CaseDash"
if not exist "%SCRATCH_ROOT%" mkdir "%SCRATCH_ROOT%"
set "BUILD_TMP=%SCRATCH_ROOT%\run-%RANDOM%-%RANDOM%"
if exist "%BUILD_TMP%" rmdir /s /q "%BUILD_TMP%" >nul 2>nul
mkdir "%BUILD_TMP%"
set "TMP=%BUILD_TMP%"
set "TEMP=%BUILD_TMP%"

set "BUILD_ROOT=%REPO_ROOT%\build"
set "CMAKE_BUILD_ROOT=%BUILD_ROOT%\cmake"
set "REPO_VCPKG_INSTALLED_DIR=%REPO_ROOT%\vcpkg"
set "CASEDASH_CACHE_ROOT_DEFAULT="
if defined CASEDASH_CACHE_ROOT (
    set "CASEDASH_CACHE_ROOT_DEFAULT=%CASEDASH_CACHE_ROOT%"
) else (
    if defined LOCALAPPDATA (
        set "CASEDASH_CACHE_ROOT_DEFAULT=%LOCALAPPDATA%\CaseDash\cache"
    ) else if defined USERPROFILE (
        set "CASEDASH_CACHE_ROOT_DEFAULT=%USERPROFILE%\.casedash\cache"
    ) else (
        set "CASEDASH_CACHE_ROOT_DEFAULT=%BUILD_ROOT%\cache"
    )
)
if not defined VCPKG_DOWNLOADS set "VCPKG_DOWNLOADS=%CASEDASH_CACHE_ROOT_DEFAULT%\vcpkg\downloads"
if not defined X_VCPKG_REGISTRIES_CACHE set "X_VCPKG_REGISTRIES_CACHE=%CASEDASH_CACHE_ROOT_DEFAULT%\vcpkg\registries"
if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
if not exist "%VCPKG_DOWNLOADS%" mkdir "%VCPKG_DOWNLOADS%"
if not exist "%X_VCPKG_REGISTRIES_CACHE%" mkdir "%X_VCPKG_REGISTRIES_CACHE%"
set "CMAKE_GENERATOR=Ninja Multi-Config"
set "NEED_CONFIGURE=0"
if exist "%CMAKE_BUILD_ROOT%\CMakeCache.txt" (
    findstr /c:"CMAKE_GENERATOR:INTERNAL=%CMAKE_GENERATOR%" "%CMAKE_BUILD_ROOT%\CMakeCache.txt" >nul
    if errorlevel 1 (
        rmdir /s /q "%CMAKE_BUILD_ROOT%"
        set "NEED_CONFIGURE=1"
    )
) else (
    set "NEED_CONFIGURE=1"
)
if not exist "%CMAKE_BUILD_ROOT%" mkdir "%CMAKE_BUILD_ROOT%"
if "%NEED_CONFIGURE%"=="0" if not exist "%CMAKE_BUILD_ROOT%\build.ninja" set "NEED_CONFIGURE=1"

set "CASEDASH_LINK_MAPS_OPTION=OFF"
if defined CASEDASH_LINK_MAPS set "CASEDASH_LINK_MAPS_OPTION=%CASEDASH_LINK_MAPS%"
set "CASEDASH_CMAKE_OPTIONS=-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCASEDASH_LINK_MAPS=%CASEDASH_LINK_MAPS_OPTION%"
if "%NEED_CONFIGURE%"=="0" (
    findstr /b /e /c:"CASEDASH_LINK_MAPS:BOOL=%CASEDASH_LINK_MAPS_OPTION%" "%CMAKE_BUILD_ROOT%\CMakeCache.txt" >nul
    if errorlevel 1 set "NEED_CONFIGURE=1"
)
if /I not "%CASEDASH_LINK_MAPS_OPTION%"=="ON" (
    del /q "%REPO_ROOT%\build\CaseDash.map" "%REPO_ROOT%\build\CaseDash.map.summary.txt" >nul 2>nul
    del /q "%REPO_ROOT%\build\CaseDashBenchmarks.map" "%REPO_ROOT%\build\CaseDashBenchmarks.map.summary.txt" >nul 2>nul
)

set "VSINSTALLDIR_NORMALIZED="
if defined VSINSTALLDIR (
    for /f "delims=" %%I in ("%VSINSTALLDIR%") do set "VSINSTALLDIR_NORMALIZED=%%I"
)

set "VCPKG_TOOLCHAIN_FILE="
set "VCPKG_EXE="
if defined VSINSTALLDIR_NORMALIZED (
    if exist "%VSINSTALLDIR_NORMALIZED%VC\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_TOOLCHAIN_FILE=%VSINSTALLDIR_NORMALIZED%VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
    )
    if exist "%VSINSTALLDIR_NORMALIZED%VC\vcpkg\vcpkg.exe" (
        set "VCPKG_EXE=%VSINSTALLDIR_NORMALIZED%VC\vcpkg\vcpkg.exe"
    )
)
if not defined VCPKG_TOOLCHAIN_FILE if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    ) else (
        echo VCPKG_ROOT is set but "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" was not found.
        exit /b 1
    )
)
if not defined VCPKG_EXE if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
    ) else (
        echo VCPKG_ROOT is set but "%VCPKG_ROOT%\vcpkg.exe" was not found.
        exit /b 1
    )
)
if exist "%REPO_ROOT%\vcpkg.json" if not defined CMAKE_TOOLCHAIN_FILE if not defined VCPKG_TOOLCHAIN_FILE (
    echo The repo uses a vcpkg manifest for test dependencies.
    echo Install vcpkg locally and set VCPKG_ROOT, or use a developer environment that provides VSINSTALLDIR with bundled vcpkg.
    exit /b 1
)

if defined CASEDASH_FORCE_CONFIGURE set "NEED_CONFIGURE=1"

if "%NEED_CONFIGURE%"=="1" (
    if defined VCPKG_TOOLCHAIN_FILE (
        cmake -S "%REPO_ROOT%" -B "%CMAKE_BUILD_ROOT%" -G "%CMAKE_GENERATOR%" %CASEDASH_CMAKE_OPTIONS% -DVCPKG_TARGET_TRIPLET=x64-windows "-DVCPKG_INSTALLED_DIR=%REPO_VCPKG_INSTALLED_DIR%" "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TOOLCHAIN_FILE%"
    ) else (
        cmake -S "%REPO_ROOT%" -B "%CMAKE_BUILD_ROOT%" -G "%CMAKE_GENERATOR%" %CASEDASH_CMAKE_OPTIONS%
    )
    if errorlevel 1 goto build_done
) else (
    echo -- CMake configure skipped; using existing build\cmake cache.
)

set "BUILD_CONFIG=Release"
set "BUILD_BENCHMARKS=0"

:parse_build_args
if "%~1"=="" goto build_args_done
if /I "%~1"=="/benchmarks" (
    set "BUILD_BENCHMARKS=1"
) else if /I "%~1"=="/benchmark" (
    set "BUILD_BENCHMARKS=1"
) else (
    set "BUILD_CONFIG=%~1"
)
shift
goto parse_build_args

:build_args_done

if "%BUILD_BENCHMARKS%"=="1" (
    cmake --build "%CMAKE_BUILD_ROOT%" --config "%BUILD_CONFIG%" --target CaseDash CaseDashTests CaseDashBenchmarks
) else (
    cmake --build "%CMAKE_BUILD_ROOT%" --config "%BUILD_CONFIG%" --target CaseDash CaseDashTests
)

:build_done
set "BUILD_RC=%errorlevel%"

del /q "%REPO_ROOT%\build\RC*" >nul 2>nul
del /q "%REPO_ROOT%\build\RD*" >nul 2>nul
del /q "%REPO_ROOT%\build\CSC*.TMP" >nul 2>nul
rmdir /s /q "%BUILD_TMP%" >nul 2>nul
exit /b %BUILD_RC%

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
