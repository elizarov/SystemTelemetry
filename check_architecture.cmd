@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "root=%~dp0"
pushd "%root%" >nul || exit /b 1

python tools\check_architecture.py %*
set "status=%errorlevel%"

popd >nul
exit /b %status%
