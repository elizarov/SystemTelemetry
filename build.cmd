@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"

call "%REPO_ROOT%\devenv.cmd"
if errorlevel 1 exit /b %errorlevel%

if not exist "%REPO_ROOT%\build" mkdir "%REPO_ROOT%\build"
set "SCRATCH_ROOT=%TEMP%"
if not defined SCRATCH_ROOT set "SCRATCH_ROOT=%TMP%"
if not defined SCRATCH_ROOT if defined LOCALAPPDATA set "SCRATCH_ROOT=%LOCALAPPDATA%\Temp"
if not defined SCRATCH_ROOT set "SCRATCH_ROOT=%REPO_ROOT%\build\tmp"
set "SCRATCH_ROOT=%SCRATCH_ROOT%\SystemTelemetry"
if not exist "%SCRATCH_ROOT%" mkdir "%SCRATCH_ROOT%"
set "BUILD_TMP=%SCRATCH_ROOT%\run-%RANDOM%-%RANDOM%"
if exist "%BUILD_TMP%" rmdir /s /q "%BUILD_TMP%" >nul 2>nul
mkdir "%BUILD_TMP%"
set "TMP=%BUILD_TMP%"
set "TEMP=%BUILD_TMP%"

set "BUILD_ROOT=%REPO_ROOT%\build"
set "CMAKE_BUILD_ROOT=%BUILD_ROOT%\cmake"
if not exist "%BUILD_ROOT%" mkdir "%BUILD_ROOT%"
set "CMAKE_GENERATOR=Ninja Multi-Config"
if exist "%CMAKE_BUILD_ROOT%\CMakeCache.txt" (
    findstr /c:"CMAKE_GENERATOR:INTERNAL=%CMAKE_GENERATOR%" "%CMAKE_BUILD_ROOT%\CMakeCache.txt" >nul
    if errorlevel 1 (
        rmdir /s /q "%CMAKE_BUILD_ROOT%"
    )
)
if not exist "%CMAKE_BUILD_ROOT%" mkdir "%CMAKE_BUILD_ROOT%"

set "VCPKG_TOOLCHAIN_FILE="
set "VCPKG_EXE="
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    ) else (
        echo VCPKG_ROOT is set but "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" was not found.
        exit /b 1
    )
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
    ) else (
        echo VCPKG_ROOT is set but "%VCPKG_ROOT%\vcpkg.exe" was not found.
        exit /b 1
    )
) else if exist "%REPO_ROOT%\vcpkg.json" if not defined CMAKE_TOOLCHAIN_FILE (
    echo The repo uses a vcpkg manifest for test dependencies.
    echo Set VCPKG_ROOT to your vcpkg install before running build.cmd.
    exit /b 1
)

if defined VCPKG_TOOLCHAIN_FILE (
    if exist "%REPO_ROOT%\vcpkg.json" (
        "%VCPKG_EXE%" install --x-manifest-root="%REPO_ROOT%" --x-install-root="%BUILD_ROOT%\vcpkg_installed" --triplet x64-windows
        if errorlevel 1 goto build_done
    )
    cmake -S "%REPO_ROOT%" -B "%CMAKE_BUILD_ROOT%" -G "%CMAKE_GENERATOR%" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DVCPKG_TARGET_TRIPLET=x64-windows "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TOOLCHAIN_FILE%"
) else (
    cmake -S "%REPO_ROOT%" -B "%CMAKE_BUILD_ROOT%" -G "%CMAKE_GENERATOR%" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)
if errorlevel 1 goto build_done

if "%~1"=="" (
    set "BUILD_CONFIG=Release"
) else (
    set "BUILD_CONFIG=%~1"
)

cmake --build "%CMAKE_BUILD_ROOT%" --config "%BUILD_CONFIG%"

:build_done
set "BUILD_RC=%errorlevel%"

del /q "%REPO_ROOT%\build\RC*" >nul 2>nul
del /q "%REPO_ROOT%\build\RD*" >nul 2>nul
del /q "%REPO_ROOT%\build\CSC*.TMP" >nul 2>nul
rmdir /s /q "%BUILD_TMP%" >nul 2>nul
exit /b %BUILD_RC%
