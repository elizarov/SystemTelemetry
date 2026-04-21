@echo off
setlocal EnableExtensions

set "root=%~dp0"
pushd "%root%" >nul || exit /b 1

python tools\source_dependency_graph.py %*
set "result=%errorlevel%"

popd >nul
exit /b %result%
