@echo off
setlocal

call "%~dp0devenv.cmd"
if errorlevel 1 exit /b %errorlevel%

nmake %*
exit /b %errorlevel%
