@echo off

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if errorlevel 1 exit /b %errorlevel%

if not defined CSC set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" (
    echo CSC compiler not found at "%CSC%".
    echo Update devenv.cmd or set CSC before running build.cmd.
    exit /b 1
)
