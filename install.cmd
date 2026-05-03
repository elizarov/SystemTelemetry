@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "INSTALL_ROOT=%ProgramW6432%"
if not defined INSTALL_ROOT set "INSTALL_ROOT=%ProgramFiles%"
set "INSTALL_DIR=%INSTALL_ROOT%\CaseDash"
set "SOURCE_EXE=%~dp0build\CaseDash.exe"
set "TARGET_EXE=%INSTALL_DIR%\CaseDash.exe"

if not exist "%SOURCE_EXE%" (
    echo Missing build output: "%SOURCE_EXE%"
    echo Build the project first with build.cmd, then run install.cmd.
    exit /b 1
)

call :ensure_admin
if errorlevel 2 exit /b 0
if errorlevel 1 exit /b 1


echo Installing to "%INSTALL_DIR%"...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if errorlevel 1 (
    echo Failed to create "%INSTALL_DIR%".
    exit /b 1
)

echo Stopping CaseDash service...
call :stop_service
if errorlevel 1 (
    echo Failed to stop CashDashService.
    exit /b 1
)

echo Stopping running CaseDash instances...
taskkill /f /im "CaseDash.exe" >nul 2>nul
set "TASKKILL_RC=%errorlevel%"
if not "%TASKKILL_RC%"=="0" (
    if not "%TASKKILL_RC%"=="128" (
        echo Failed to stop running CaseDash.exe instances.
        exit /b 1
    )
)
call :wait_for_process_exit
if errorlevel 1 (
    echo Timed out waiting for running CaseDash.exe instances to exit.
    exit /b 1
)

copy /y "%SOURCE_EXE%" "%TARGET_EXE%" >nul
if errorlevel 1 (
    echo Failed to copy "%SOURCE_EXE%" to "%TARGET_EXE%".
    exit /b 1
)
if not exist "%TARGET_EXE%" (
    echo Installation did not produce "%TARGET_EXE%".
    exit /b 1
)

echo Installation complete.
echo Installed executable: "%TARGET_EXE%"
echo Configure auto-start from the app popup menu if needed.
exit /b 0

:ensure_admin
net session >nul 2>nul
if not errorlevel 1 exit /b 0

echo Requesting administrator access...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$process = Start-Process -FilePath $env:ComSpec -ArgumentList '/c ""%~f0""' -WorkingDirectory '%~dp0' -Verb RunAs -Wait -PassThru; exit $process.ExitCode"
if errorlevel 1 (
    echo Administrator approval is required to install into Program Files.
    exit /b 1
)
exit /b 2

:stop_service
setlocal EnableExtensions
sc query "CashDashService" >nul 2>nul
if errorlevel 1060 exit /b 0
if errorlevel 1 exit /b 1

sc stop "CashDashService" >nul 2>nul
set "STOP_RC=%errorlevel%"
if not "%STOP_RC%"=="0" (
    if not "%STOP_RC%"=="1062" (
        exit /b 1
    )
)

set /a SERVICE_WAIT_RETRIES=30
:stop_service_wait_loop
set "SERVICE_STATE="
for /f "tokens=4" %%S in ('sc query "CashDashService" ^| findstr /R /C:"STATE"') do set "SERVICE_STATE=%%S"
if "%SERVICE_STATE%"=="STOPPED" exit /b 0
if "%SERVICE_WAIT_RETRIES%"=="0" exit /b 1
timeout /t 1 /nobreak >nul
set /a SERVICE_WAIT_RETRIES-=1
goto stop_service_wait_loop

:wait_for_process_exit
setlocal EnableExtensions
set /a WAIT_RETRIES=30
:wait_for_process_exit_loop
tasklist /fi "IMAGENAME eq CaseDash.exe" 2>nul | find /i "CaseDash.exe" >nul
if errorlevel 1 (
    exit /b 0
)
if "%WAIT_RETRIES%"=="0" (
    exit /b 1
)
timeout /t 1 /nobreak >nul
set /a WAIT_RETRIES-=1
goto wait_for_process_exit_loop

