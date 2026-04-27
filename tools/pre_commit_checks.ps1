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

$hasStagedCppFiles = [bool]$stagedFiles

$stashCreated = $false
$stashMessage = 'systemtelemetry-pre-commit-format'

& git -C $repoRoot diff --quiet --no-ext-diff --ignore-submodules --
$hasUnstagedChanges = ($LASTEXITCODE -ne 0)
$untrackedFiles = & git -C $repoRoot ls-files --others --exclude-standard
$hasUntrackedFiles = [bool]$untrackedFiles

if ($hasUnstagedChanges -or $hasUntrackedFiles) {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $stashOutput = & git -C $repoRoot stash push --keep-index --include-untracked -q -m $stashMessage 2>&1
        $stashStatus = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

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
    $hookStatus = 0

    if ($hasStagedCppFiles) {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'tools\run_clang_format.ps1') -Root $repoRoot -Mode fix -Scope staged -Restage
        $formatStatus = $LASTEXITCODE
        if ($formatStatus -ne 0) {
            $hookStatus = $formatStatus
        }
    }

    if ($hookStatus -eq 0) {
        Write-Host 'pre-commit: running lint checks'
        & cmd.exe /c "`"$repoRoot\lint.cmd`""
        $lintStatus = $LASTEXITCODE
        if ($lintStatus -ne 0) {
            Write-Host 'pre-commit: lint checks failed'
            $hookStatus = $lintStatus
        }
    }
} catch {
    Restore-PreCommitStash
    throw
}

Restore-PreCommitStash
exit $hookStatus
