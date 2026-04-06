@echo off
setlocal EnableExtensions

cd /d "%~dp0"

call :ensure_admin
if errorlevel 2 exit /b 0
if errorlevel 1 exit /b 1

set "INSTALL_ROOT=%ProgramW6432%"
if not defined INSTALL_ROOT set "INSTALL_ROOT=%ProgramFiles%"
set "INSTALL_DIR=%INSTALL_ROOT%\SystemTelemetry"
set "SOURCE_EXE=%~dp0build\SystemTelemetry.exe"
set "TARGET_EXE=%INSTALL_DIR%\SystemTelemetry.exe"
set "RUN_KEY=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run"
set "RUN_VALUE=SystemTelemetry"

echo Building SystemTelemetry...
call "%~dp0build.cmd"
if errorlevel 1 (
    echo Build failed. Installation aborted.
    exit /b 1
)

if not exist "%SOURCE_EXE%" (
    echo Missing build output: "%SOURCE_EXE%"
    exit /b 1
)


echo Installing to "%INSTALL_DIR%"...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if errorlevel 1 (
    echo Failed to create "%INSTALL_DIR%".
    exit /b 1
)

copy /y "%SOURCE_EXE%" "%TARGET_EXE%" >nul
if errorlevel 1 (
    echo Failed to copy "%SOURCE_EXE%" to "%TARGET_EXE%".
    exit /b 1
)


reg add "%RUN_KEY%" /v "%RUN_VALUE%" /t REG_SZ /d "\"%TARGET_EXE%\"" /f >nul
if errorlevel 1 (
    echo Failed to register startup entry.
    exit /b 1
)

echo Installation complete.
echo Installed executable: "%TARGET_EXE%"
echo Startup entry: "%RUN_KEY%\%RUN_VALUE%"
exit /b 0

:ensure_admin
net session >nul 2>nul
if not errorlevel 1 exit /b 0

echo Requesting administrator access...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -WorkingDirectory '%~dp0' -Verb RunAs"
if errorlevel 1 (
    echo Administrator approval is required to install into Program Files and register machine-wide startup.
    exit /b 1
)
exit /b 2


