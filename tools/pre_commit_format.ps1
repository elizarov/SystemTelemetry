param()

$ErrorActionPreference = 'Stop'

$repoRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repoRoot)) {
    exit 1
}

$repoRoot = $repoRoot.Trim()
$stagedFiles = & git -C $repoRoot diff --cached --name-only --diff-filter=ACMR -- '*.cpp' '*.h'
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $stagedFiles) {
    exit 0
}

$stashCreated = $false
$stashMessage = 'systemtelemetry-pre-commit-format'

& git -C $repoRoot diff --quiet --no-ext-diff --ignore-submodules --
$hasUnstagedChanges = ($LASTEXITCODE -ne 0)
$untrackedFiles = & git -C $repoRoot ls-files --others --exclude-standard
$hasUntrackedFiles = [bool]$untrackedFiles

if ($hasUnstagedChanges -or $hasUntrackedFiles) {
    $stashOutput = & git -C $repoRoot stash push --keep-index --include-untracked -q -m $stashMessage 2>&1
    $stashStatus = $LASTEXITCODE
    if ($stashStatus -ne 0) {
        Write-Host 'pre-commit: unable to stash unstaged changes'
        Write-Host $stashOutput
        exit $stashStatus
    }

    if ($stashOutput -and $stashOutput -ne 'No local changes to save') {
        $stashCreated = $true
    }
}

function Restore-PreCommitStash {
    if (-not $stashCreated) {
        return
    }

    & git -C $repoRoot stash pop -q *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'pre-commit: formatted staged files, but failed to restore unstaged changes from stash'
        Write-Host "Run 'git stash list' and restore the top '$stashMessage' entry manually."
        exit 1
    }
}

try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools\run_clang_format.ps1') -Root $repoRoot -Mode fix -Scope staged -Restage
    $formatStatus = $LASTEXITCODE
    if ($formatStatus -ne 0) {
        Restore-PreCommitStash
        exit $formatStatus
    }
} catch {
    Restore-PreCommitStash
    throw
}

Restore-PreCommitStash
exit 0
