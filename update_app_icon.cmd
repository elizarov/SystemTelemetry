@echo off
setlocal
for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\tools\update_app_icon.ps1" %*
exit /b %errorlevel%
