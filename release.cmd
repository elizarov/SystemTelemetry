@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
    echo Usage: release.cmd ^<version^> [--force]
    echo Example: release.cmd 0.1
    exit /b 1
)

set "RELEASE_VERSION=%~1"
set "RELEASE_FORCE="
if not "%~2"=="" (
    if /I "%~2"=="--force" (
        set "RELEASE_FORCE=1"
    ) else (
        echo Unknown release option: %~2
        echo Usage: release.cmd ^<version^> [--force]
        exit /b 1
    )
)
if not "%~3"=="" (
    echo Unknown release option: %~3
    echo Usage: release.cmd ^<version^> [--force]
    exit /b 1
)
set "RELEASE_VERSION_VALID="
echo %RELEASE_VERSION%| findstr /R "^[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 set "RELEASE_VERSION_VALID=1"
echo %RELEASE_VERSION%| findstr /R "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 set "RELEASE_VERSION_VALID=1"
if not defined RELEASE_VERSION_VALID (
    echo Version must use major.minor or major.minor.patch format.
    exit /b 1
)

for %%I in ("%~dp0.") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo release.cmd must run inside the CaseDash Git repository.
    exit /b 1
)

git diff --quiet -- VERSION
if errorlevel 1 (
    echo VERSION has uncommitted changes. Commit or restore it before release.
    exit /b 1
)

if defined RELEASE_FORCE (
    echo Force release enabled; existing tag v%RELEASE_VERSION% will be replaced.
)

set /p "CONFIRM=Prepare CaseDash %RELEASE_VERSION% release, update VERSION and changelog if needed, run validation, tag, and push? Type %RELEASE_VERSION% to continue: "
if not "%CONFIRM%"=="%RELEASE_VERSION%" (
    echo Release cancelled.
    exit /b 1
)

call powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO_ROOT%\tools\release_changelog.ps1" -Mode Prepare -Version "%RELEASE_VERSION%"
if errorlevel 1 exit /b %errorlevel%

> VERSION echo %RELEASE_VERSION%

git add VERSION docs/changelog.md
git diff --cached --quiet -- VERSION docs/changelog.md
if errorlevel 1 (
    git commit -m "Changed version to %RELEASE_VERSION%" -- VERSION docs/changelog.md
    if errorlevel 1 exit /b %errorlevel%
) else (
    echo VERSION and docs\changelog.md already contain release state; continuing without a release metadata commit.
)

call "%REPO_ROOT%\format.cmd"
if errorlevel 1 exit /b %errorlevel%

call "%REPO_ROOT%\lint.cmd"
if errorlevel 1 exit /b %errorlevel%

call "%REPO_ROOT%\build.cmd" /benchmarks
if errorlevel 1 exit /b %errorlevel%

call "%REPO_ROOT%\test.cmd"
if errorlevel 1 exit /b %errorlevel%

git diff --quiet --exit-code
if errorlevel 1 (
    echo Validation left uncommitted changes. Review them before tagging.
    exit /b 1
)

set "RELEASE_TAG=v%RELEASE_VERSION%"
git rev-parse -q --verify "refs/tags/%RELEASE_TAG%" >nul 2>nul
if not errorlevel 1 if not defined RELEASE_FORCE (
    echo Tag %RELEASE_TAG% already exists.
    exit /b 1
)

if defined RELEASE_FORCE (
    git tag -f -a "%RELEASE_TAG%" -m "CaseDash %RELEASE_VERSION%"
) else (
    git tag -a "%RELEASE_TAG%" -m "CaseDash %RELEASE_VERSION%"
)
if errorlevel 1 exit /b %errorlevel%

git push origin HEAD
if errorlevel 1 exit /b %errorlevel%

if defined RELEASE_FORCE (
    git push --force origin "%RELEASE_TAG%"
) else (
    git push origin "%RELEASE_TAG%"
)
if errorlevel 1 exit /b %errorlevel%

echo Release tag %RELEASE_TAG% pushed. GitHub Actions will build and publish the release.
