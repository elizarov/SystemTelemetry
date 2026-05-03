@echo off
setlocal EnableExtensions

for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\web\scripts\build-web.ps1" %*
exit /b %errorlevel%
