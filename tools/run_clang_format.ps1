param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('check', 'fix')]
    [string]$Mode = 'check',

    [ValidateSet('all', 'changed', 'staged')]
    [string]$Scope = 'all',

    [switch]$Restage
)

$ErrorActionPreference = 'Stop'

function Get-RelativeRepoPath {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    $repoRootUri = [System.Uri]::new(([System.IO.Path]::GetFullPath($RepoRoot.TrimEnd('\')) + '\'))
    $fileUri = [System.Uri]::new([System.IO.Path]::GetFullPath($FullPath))
    return [System.Uri]::UnescapeDataString(
        $repoRootUri.MakeRelativeUri($fileUri).ToString().Replace('/', '\')
    )
}

function Resolve-ClangFormatPath {
    param(
        [string]$RepoRoot
    )

    $candidates = [System.Collections.Generic.List[string]]::new()

    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        foreach ($candidate in @(
            (Join-Path $env:VSINSTALLDIR 'VC\Tools\Llvm\x64\bin\clang-format.exe')
            (Join-Path $env:VSINSTALLDIR 'VC\Tools\Llvm\bin\clang-format.exe')
        )) {
            if (-not [string]::IsNullOrWhiteSpace($candidate) -and -not $candidates.Contains($candidate)) {
                $candidates.Add($candidate)
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command clang-format.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $devenvCmd = Join-Path $RepoRoot 'devenv.cmd'
    if (Test-Path -LiteralPath $devenvCmd) {
        $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clang-format.exe" 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $probe) {
            return $probe.Trim()
        }
    }

    foreach ($pattern in @(
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-format.exe'
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\bin\clang-format.exe'
    )) {
        $discovered = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($discovered) {
            return $discovered.FullName
        }
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Test-EligibleCppPath {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    if (-not (Test-Path -LiteralPath $FullPath -PathType Leaf)) {
        return $false
    }

    $normalizedPath = [System.IO.Path]::GetFullPath($FullPath)
    $relativePath = Get-RelativeRepoPath -RepoRoot $RepoRoot -FullPath $normalizedPath
    $relativePath = $relativePath.Replace('\', '/')

    if ($relativePath -notmatch '^(src|tests)/') {
        return $false
    }
    if ($relativePath -match '^src/vendor/') {
        return $false
    }
    if ([System.IO.Path]::GetExtension($normalizedPath) -notin @('.cpp', '.h')) {
        return $false
    }

    return $true
}

function Get-AllCppFiles {
    param(
        [string]$RepoRoot
    )

    $files = & git -C $RepoRoot -c core.quotepath=off -c core.safecrlf=false ls-files --cached --others --exclude-standard -- '*.cpp' '*.h' 2>$null
    foreach ($relativePath in ($files | Sort-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $fullPath = Join-Path $RepoRoot $relativePath.Trim()
        if (Test-EligibleCppPath -RepoRoot $RepoRoot -FullPath $fullPath) {
            Get-Item -LiteralPath $fullPath
        }
    }
}

function Get-ChangedCppFiles {
    param(
        [string]$RepoRoot
    )

    $pathSpecs = @('*.cpp', '*.h')
    $changedFiles = [System.Collections.Generic.List[string]]::new()

    $headExists = $false
    & git -C $RepoRoot rev-parse --verify HEAD *> $null
    if ($LASTEXITCODE -eq 0) {
        $headExists = $true
    }

    if ($headExists) {
        $gitDiffOutput = & git -C $RepoRoot -c core.safecrlf=false diff --name-only --diff-filter=ACMR HEAD -- $pathSpecs 2>$null
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    } else {
        $gitDiffOutput = & git -C $RepoRoot -c core.safecrlf=false diff --name-only --diff-filter=ACMR --cached -- $pathSpecs 2>$null
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    }

    $untrackedOutput = & git -C $RepoRoot -c core.safecrlf=false ls-files --others --exclude-standard -- $pathSpecs 2>$null
    foreach ($entry in $untrackedOutput) {
        if (-not [string]::IsNullOrWhiteSpace($entry)) {
            $changedFiles.Add($entry.Trim())
        }
    }

    foreach ($relativePath in ($changedFiles | Sort-Object -Unique)) {
        $fullPath = Join-Path $RepoRoot $relativePath
        if (Test-EligibleCppPath -RepoRoot $RepoRoot -FullPath $fullPath) {
            Get-Item -LiteralPath $fullPath
        }
    }
}

function Get-StagedCppFiles {
    param(
        [string]$RepoRoot
    )

    $pathSpecs = @('*.cpp', '*.h')
    $stagedOutput = & git -C $RepoRoot -c core.safecrlf=false diff --cached --name-only --diff-filter=ACMR -- $pathSpecs 2>$null
    foreach ($relativePath in ($stagedOutput | Sort-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $fullPath = Join-Path $RepoRoot $relativePath.Trim()
        if (Test-EligibleCppPath -RepoRoot $RepoRoot -FullPath $fullPath) {
            Get-Item -LiteralPath $fullPath
        }
    }
}

function Invoke-FormatFile {
    param(
        [string]$ClangFormatPath,
        [System.IO.FileInfo]$File
    )

    & $ClangFormatPath -i $File.FullName
    return ($LASTEXITCODE -eq 0)
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$clangFormat = Resolve-ClangFormatPath -RepoRoot $resolvedRoot
if (-not $clangFormat) {
    Write-Error 'clang-format.exe was not found. Install the Visual Studio LLVM tools or add clang-format to PATH.'
}

$files = switch ($Scope) {
    'all' { @(Get-AllCppFiles -RepoRoot $resolvedRoot) }
    'changed' { @(Get-ChangedCppFiles -RepoRoot $resolvedRoot) }
    'staged' { @(Get-StagedCppFiles -RepoRoot $resolvedRoot) }
    default { throw "Unsupported scope '$Scope'." }
}

if ($files.Count -eq 0) {
    if ($Scope -eq 'all') {
        Write-Host 'No non-vendored C++ source files were found.'
        exit 1
    }

    Write-Host ("No eligible {0} C++ source files were found." -f $Scope)
    exit 0
}

$failed = $false
foreach ($file in $files) {
    if ($Mode -eq 'fix') {
        if (-not (Invoke-FormatFile -ClangFormatPath $clangFormat -File $file)) {
            $failed = $true
            continue
        }

        if ($Restage) {
            $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName
            & git -C $resolvedRoot -c core.safecrlf=false add -- $relativePath
            if ($LASTEXITCODE -ne 0) {
                $failed = $true
            }
        }
    } else {
        & $clangFormat --dry-run --Werror $file.FullName
        if ($LASTEXITCODE -ne 0) {
            $failed = $true
        }
    }
}

if ($failed) {
    if ($Mode -eq 'fix') {
        Write-Host 'Formatting failed.'
    } else {
        Write-Host 'Formatting is required. Run "format fix" or "format fix changed".'
    }

    exit 1
}

if ($Mode -eq 'fix') {
    Write-Host ("Formatted {0} {1} file{2}." -f $files.Count, $Scope, $(if ($files.Count -eq 1) { '' } else { 's' }))
} else {
    Write-Host ("Checked {0} {1} file{2}. Formatting is up to date." -f $files.Count, $Scope, $(if ($files.Count -eq 1) { '' } else { 's' }))
}
