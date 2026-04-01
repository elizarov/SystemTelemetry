@echo off
setlocal

call "%~dp0devenv.cmd"
if errorlevel 1 exit /b %errorlevel%

if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0build\tmp" mkdir "%~dp0build\tmp"
set "BUILD_TMP=%~dp0build\tmp\run-%RANDOM%-%RANDOM%"
if exist "%BUILD_TMP%" rmdir /s /q "%BUILD_TMP%" >nul 2>nul
mkdir "%BUILD_TMP%"
set "TMP=%BUILD_TMP%"
set "TEMP=%BUILD_TMP%"

nmake %*
set "BUILD_RC=%errorlevel%"

del /q "%~dp0build\RC*" >nul 2>nul
del /q "%~dp0build\RD*" >nul 2>nul
del /q "%~dp0build\CSC*.TMP" >nul 2>nul
rmdir /s /q "%BUILD_TMP%" >nul 2>nul
exit /b %BUILD_RC%
