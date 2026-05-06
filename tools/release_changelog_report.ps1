[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [string]$OutputPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($RepoRoot -eq '') {
    $RepoRoot = Join-Path $PSScriptRoot '..'
}

$resolvedRoot = (Resolve-Path $RepoRoot).Path
if ($OutputPath -eq '') {
    $OutputPath = Join-Path $resolvedRoot 'build\release_changelog_report.md'
}

Push-Location $resolvedRoot
try {
    & git rev-parse --is-inside-work-tree *> $null
    if ($LASTEXITCODE -ne 0) {
        throw 'release_changelog_report.ps1 must run inside the CaseDash Git repository.'
    }

    $lastTag = (& git describe --tags --abbrev=0 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or $lastTag -eq '') {
        throw 'No reachable Git tag was found. Create a release tag before generating a changelog report.'
    }

    $range = "$lastTag..HEAD"
    $commitSubjects = @(& git log --reverse --pretty=format:'- %h %s' $range)
    $changedFiles = @(& git diff --name-status --find-renames $range)
    $commitStats = @(& git log --reverse --stat --find-renames --date=short --pretty=format:'### %h %ad %s%n%b' $range)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Release Changelog Source Report')
    $lines.Add('')
    $lines.Add("- Range: ``$range``")
    $lines.Add("- Last tag: ``$lastTag``")
    $lines.Add("- Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
    $lines.Add('')
    $lines.Add('## Commit Subjects')
    $lines.Add('')
    if ($commitSubjects.Count -eq 0) {
        $lines.Add('- No commits since the latest tag.')
    } else {
        foreach ($line in $commitSubjects) {
            $lines.Add($line)
        }
    }
    $lines.Add('')
    $lines.Add('## Changed Files')
    $lines.Add('')
    if ($changedFiles.Count -eq 0) {
        $lines.Add('- No changed files since the latest tag.')
    } else {
        foreach ($line in $changedFiles) {
            $lines.Add($line)
        }
    }
    $lines.Add('')
    $lines.Add('## Commit Stats')
    $lines.Add('')
    if ($commitStats.Count -eq 0) {
        $lines.Add('- No commit stats since the latest tag.')
    } else {
        foreach ($line in $commitStats) {
            $lines.Add($line)
        }
    }

    $parent = Split-Path -Parent $OutputPath
    if ($parent -ne '') {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($OutputPath, (($lines -join "`r`n") + "`r`n"), $utf8NoBom)
    Write-Host "Wrote release changelog source report to $OutputPath"
}
finally {
    Pop-Location
}
