param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('check', 'fix')]
    [string]$Mode = 'check',

    [ValidateSet('all', 'changed', 'staged')]
    [string]$Scope = 'all',

    [string]$TargetFile,

    [switch]$Stdout,

    [switch]$Restage
)

$ErrorActionPreference = 'Stop'
$formatStarted = [System.Diagnostics.Stopwatch]::StartNew()

function Format-Elapsed {
    param(
        [System.TimeSpan]$Elapsed
    )

    $seconds = $Elapsed.TotalSeconds
    if ($seconds -lt 1.0) {
        return ("{0}ms" -f [int][Math]::Round($seconds * 1000.0))
    }

    return [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, '{0:0.000}s', $seconds)
}

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

    $scriptRepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $candidateRoots = @($RepoRoot, $scriptRepoRoot) | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | Select-Object -Unique
    foreach ($candidateRoot in $candidateRoots) {
        $devenvCmd = Join-Path $candidateRoot 'devenv.cmd'
        if ($env:GITHUB_ACTIONS -ne 'true' -and (Test-Path -LiteralPath $devenvCmd)) {
            $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clang-format.exe" 2>$null | Select-Object -First 1
            if ($LASTEXITCODE -eq 0 -and $probe) {
                return $probe.Trim()
            }
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
    $fileName = [System.IO.Path]::GetFileName($normalizedPath)
    if ($fileName -eq 'board_gigabyte_siv_bridge.cpp' -or $fileName -eq 'board_msi_center_bridge.cpp') {
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

function Get-TargetCppFile {
    param(
        [string]$RepoRoot,
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        throw '--file requires a non-empty path.'
    }
    if ([System.IO.Path]::IsPathRooted($RelativePath)) {
        throw '--file expects a path relative to --root.'
    }

    $rootPrefix = [System.IO.Path]::GetFullPath($RepoRoot.TrimEnd('\') + '\')
    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $RelativePath))
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw '--file must stay inside --root.'
    }
    if (-not (Test-EligibleCppPath -RepoRoot $RepoRoot -FullPath $fullPath)) {
        throw ("--file target is not an eligible formatter input: {0}" -f $RelativePath)
    }

    Get-Item -LiteralPath $fullPath
}

function Normalize-LineEndings {
    param(
        [string]$Text
    )

    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Convert-ToFileLineEndings {
    param(
        [string]$Text
    )

    return (Normalize-LineEndings -Text $Text).Replace("`n", "`r`n")
}

function Write-TextFileWithRetry {
    param(
        [string]$Path,
        [string]$Text
    )

    for ($attempt = 0; $attempt -lt 5; ++$attempt) {
        try {
            [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
            return
        } catch {
            if ($attempt -eq 4) {
                throw
            }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Get-LeadingSpaceCount {
    param(
        [string]$Line
    )

    $match = [regex]::Match($Line, '^( *)')
    return $match.Groups[1].Value.Length
}

function Get-NormalizedIndentLength {
    param(
        [int]$Length
    )

    if (($Length % 4) -eq 0) {
        return $Length
    }
    return [Math]::Max(0, [Math]::Floor(($Length + 2) / 4) * 4)
}

function Normalize-LeadingIndent {
    param(
        [string]$Line
    )

    if ($Line -notmatch '^( +)\S') {
        return $Line
    }

    $leadingSpaceCount = $matches[1].Length
    $normalizedSpaceCount = Get-NormalizedIndentLength -Length $leadingSpaceCount
    if ($normalizedSpaceCount -eq $leadingSpaceCount) {
        return $Line
    }
    return (' ' * $normalizedSpaceCount) + $Line.TrimStart()
}

function Format-ProjectText {
    param(
        [string]$Text
    )

    $normalizedText = Normalize-LineEndings -Text $Text
    $lines = $normalizedText -split "`n", -1
    $contentLineCount = $lines.Count
    $hasTrailingNewline = $false
    if ($contentLineCount -gt 0 -and $lines[$contentLineCount - 1] -eq '') {
        $hasTrailingNewline = $true
        --$contentLineCount
    }

    $mergedLines = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $contentLineCount; ++$i) {
        $line = $lines[$i]
        $line = [regex]::Replace($line, '(\S) {2,}\?', '$1 ?')
        $line = Normalize-LeadingIndent -Line $line

        if ($line.TrimEnd().EndsWith('?') -and -not $line.TrimStart().StartsWith('//') -and ($i + 1) -lt $contentLineCount) {
            $nextLine = [regex]::Replace($lines[$i + 1], '(\S) {2,}\?', '$1 ?')
            $nextLine = Normalize-LeadingIndent -Line $nextLine
            if ($nextLine.TrimEnd().EndsWith(':') -and -not $nextLine.TrimStart().StartsWith('//')) {
                $joinedLine = $line.TrimEnd() + ' ' + $nextLine.Trim()
                if ($joinedLine.Length -le 120) {
                    $mergedLines.Add($joinedLine)
                    ++$i
                    continue
                }
            }
        }

        $mergedLines.Add($line)
    }

    $formattedLines = [System.Collections.Generic.List[string]]::new()
    $activeTernaryContinuationIndent = $null
    foreach ($line in $mergedLines) {
        if ($null -ne $activeTernaryContinuationIndent -and $line.Trim().Length -gt 0) {
            $leadingSpaceCount = Get-LeadingSpaceCount -Line $line
            if ($leadingSpaceCount -lt $activeTernaryContinuationIndent) {
                $activeTernaryContinuationIndent = $null
            } elseif ($leadingSpaceCount -gt $activeTernaryContinuationIndent) {
                $line = (' ' * $activeTernaryContinuationIndent) + $line.TrimStart()
            }
        }

        $formattedLines.Add($line)
        if ($line.TrimEnd().EndsWith(':') -and $line.Contains('?')) {
            if ($null -eq $activeTernaryContinuationIndent) {
                $activeTernaryContinuationIndent = Get-LeadingSpaceCount -Line $line
                $activeTernaryContinuationIndent += 4
            }
        } elseif ($null -ne $activeTernaryContinuationIndent -and $line.Trim().Length -gt 0) {
            $activeTernaryContinuationIndent = $null
        }
    }

    $result = [string]::Join("`n", $formattedLines)
    if ($hasTrailingNewline) {
        $result += "`n"
    }
    return $result
}

function Invoke-ClangFormatText {
    param(
        [string]$ClangFormatPath,
        [System.IO.FileInfo]$File
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ClangFormatPath
    $startInfo.Arguments = '"' + $File.FullName.Replace('"', '\"') + '"'
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    $startInfo.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $startInfo.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = Format-ProjectText -Text $stdout
        Stderr = $stderr
    }
}

function Invoke-FormatFile {
    param(
        [string]$ClangFormatPath,
        [System.IO.FileInfo]$File
    )

    $result = Invoke-ClangFormatText -ClangFormatPath $ClangFormatPath -File $File
    if ($result.ExitCode -ne 0) {
        if (-not [string]::IsNullOrWhiteSpace($result.Stderr)) {
            Write-Error $result.Stderr
        }
        return $false
    }

    $originalText = [System.IO.File]::ReadAllText($File.FullName)
    $formattedText = Convert-ToFileLineEndings -Text $result.Stdout
    if ($originalText -ne $formattedText) {
        Write-TextFileWithRetry -Path $File.FullName -Text $formattedText
    }
    return $true
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$clangFormat = Resolve-ClangFormatPath -RepoRoot $resolvedRoot
if (-not $clangFormat) {
    Write-Error 'clang-format.exe was not found. Install the Visual Studio LLVM tools or add clang-format to PATH.'
}

if ($Stdout -and [string]::IsNullOrWhiteSpace($TargetFile)) {
    throw '--stdout requires --file.'
}
if (-not [string]::IsNullOrWhiteSpace($TargetFile) -and $Scope -eq 'changed') {
    throw '--file is incompatible with changed scope.'
}

$files = if (-not [string]::IsNullOrWhiteSpace($TargetFile)) {
    @(Get-TargetCppFile -RepoRoot $resolvedRoot -RelativePath $TargetFile)
} else {
    switch ($Scope) {
        'all' { @(Get-AllCppFiles -RepoRoot $resolvedRoot) }
        'changed' { @(Get-ChangedCppFiles -RepoRoot $resolvedRoot) }
        'staged' { @(Get-StagedCppFiles -RepoRoot $resolvedRoot) }
        default { throw "Unsupported scope '$Scope'." }
    }
}

if ($files.Count -eq 0) {
    if ($Scope -eq 'all') {
        Write-Host 'No non-vendored C++ source files were found.'
        exit 1
    }

    Write-Host ("No eligible {0} C++ source files were found." -f $Scope)
    exit 0
}

if ($Stdout) {
    $result = Invoke-ClangFormatText -ClangFormatPath $clangFormat -File $files[0]
    if ($result.ExitCode -ne 0) {
        if (-not [string]::IsNullOrWhiteSpace($result.Stderr)) {
            Write-Error $result.Stderr
        }
        exit $result.ExitCode
    }
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
    [Console]::Write($result.Stdout)
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
        $result = Invoke-ClangFormatText -ClangFormatPath $clangFormat -File $file
        if ($result.ExitCode -ne 0) {
            if (-not [string]::IsNullOrWhiteSpace($result.Stderr)) {
                Write-Error $result.Stderr
            }
            $failed = $true
            continue
        }
        $originalText = [System.IO.File]::ReadAllText($file.FullName)
        if ((Normalize-LineEndings -Text $originalText) -ne $result.Stdout) {
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
    Write-Host ("Formatted {0} {1} file{2} in {3}." -f `
            $files.Count,
            $Scope,
            $(if ($files.Count -eq 1) { '' } else { 's' }),
            (Format-Elapsed -Elapsed $formatStarted.Elapsed))
} else {
    Write-Host ("Checked {0} {1} file{2} in {3}. Formatting is up to date." -f `
            $files.Count,
            $Scope,
            $(if ($files.Count -eq 1) { '' } else { 's' }),
            (Format-Elapsed -Elapsed $formatStarted.Elapsed))
}
