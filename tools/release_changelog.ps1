[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Prepare', 'Extract')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$RepoRoot = '',

    [string]$OutputPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Normalize-Lines {
    param([string]$Text)
    return (($Text -replace "`r`n", "`n") -replace "`r", "`n").TrimStart([char]0xFEFF)
}

function Get-TopChunk {
    param([string]$Text)

    $normalized = Normalize-Lines -Text $Text
    $lines = @($normalized -split "`n", -1)
    $separatorIndex = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].Trim() -eq '---') {
            $separatorIndex = $index
            break
        }
    }

    if ($separatorIndex -lt 0) {
        return [pscustomobject]@{
            First = $normalized.Trim()
            Rest = ''
            HasSeparator = $false
        }
    }

    $firstLines = @()
    if ($separatorIndex -gt 0) {
        $firstLines = $lines[0..($separatorIndex - 1)]
    }
    $restLines = @()
    if ($separatorIndex + 1 -lt $lines.Count) {
        $restLines = $lines[($separatorIndex + 1)..($lines.Count - 1)]
    }

    return [pscustomobject]@{
        First = (($firstLines -join "`n").Trim())
        Rest = (($restLines -join "`n").Trim())
        HasSeparator = $true
    }
}

function Get-FirstNonEmptyLine {
    param([string]$Text)

    foreach ($line in @($Text -split "`n")) {
        $trimmed = $line.Trim()
        if ($trimmed -ne '') {
            return $trimmed
        }
    }
    return ''
}

function Get-ChunkBody {
    param(
        [string]$Chunk,
        [string]$ExpectedHeader
    )

    $lines = @($Chunk -split "`n", -1)
    $headerIndex = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].Trim() -eq '') {
            continue
        }
        if ($lines[$index].Trim() -eq $ExpectedHeader) {
            $headerIndex = $index
        }
        break
    }

    if ($headerIndex -lt 0) {
        return $Chunk.Trim()
    }

    if ($headerIndex + 1 -ge $lines.Count) {
        return ''
    }
    return (($lines[($headerIndex + 1)..($lines.Count - 1)] -join "`n").Trim())
}

function Assert-HasReleaseBullets {
    param(
        [string]$Body,
        [string]$Context
    )

    foreach ($line in @($Body -split "`n")) {
        if ($line.TrimStart().StartsWith('- ')) {
            return
        }
    }

    throw "$Context must contain at least one '- ' changelog bullet."
}

function Write-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )

    $parent = Split-Path -Parent $Path
    if ($parent -ne '') {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, (($Text -replace "`n", "`r`n").TrimEnd() + "`r`n"), $utf8NoBom)
}

if ($RepoRoot -eq '') {
    $RepoRoot = Join-Path $PSScriptRoot '..'
}

$resolvedRoot = (Resolve-Path $RepoRoot).Path
$changelogPath = Join-Path $resolvedRoot 'docs\changelog.md'
$expectedHeader = "## v$Version"

if (-not (Test-Path -LiteralPath $changelogPath)) {
    throw "docs\changelog.md is missing. Run the release-changelog skill before preparing a release."
}

$text = Get-Content -LiteralPath $changelogPath -Raw
if ($text.Trim() -eq '') {
    throw "docs\changelog.md is empty. Run the release-changelog skill before preparing a release."
}

$chunks = Get-TopChunk -Text $text
if ($chunks.First -eq '') {
    throw "The top docs\changelog.md chunk is empty. Add release notes before preparing a release."
}

$firstLine = Get-FirstNonEmptyLine -Text $chunks.First

if ($Mode -eq 'Prepare') {
    if ($firstLine -eq $expectedHeader) {
        $body = Get-ChunkBody -Chunk $chunks.First -ExpectedHeader $expectedHeader
        Assert-HasReleaseBullets -Body $body -Context 'The stamped top changelog chunk'
        Write-Host "docs\changelog.md already starts with $expectedHeader."
        exit 0
    }

    if ($firstLine.StartsWith('## ')) {
        throw "The top docs\changelog.md header is '$firstLine', but this release requires '$expectedHeader'."
    }

    Assert-HasReleaseBullets -Body $chunks.First -Context 'The top changelog draft'

    $updatedFirst = "$expectedHeader`n`n$($chunks.First.Trim())"
    if ($chunks.HasSeparator) {
        $updated = "$updatedFirst`n`n---`n$($chunks.Rest)"
    } else {
        $updated = $updatedFirst
    }

    Write-Utf8NoBom -Path $changelogPath -Text $updated
    Write-Host "Stamped docs\changelog.md with $expectedHeader."
    exit 0
}

if ($firstLine -ne $expectedHeader) {
    throw "The top docs\changelog.md chunk must start with '$expectedHeader' before publishing release notes."
}

$releaseBody = Get-ChunkBody -Chunk $chunks.First -ExpectedHeader $expectedHeader
Assert-HasReleaseBullets -Body $releaseBody -Context 'The release notes body'

if ($OutputPath -ne '') {
    $resolvedOutput = $OutputPath
    if (-not [System.IO.Path]::IsPathRooted($resolvedOutput)) {
        $resolvedOutput = Join-Path $resolvedRoot $resolvedOutput
    }
    Write-Utf8NoBom -Path $resolvedOutput -Text $releaseBody
    Write-Host "Wrote release notes to $resolvedOutput"
} else {
    Write-Output $releaseBody
}
