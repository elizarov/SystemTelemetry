param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('check', 'fix')]
    [string]$Mode = 'check',

    [ValidateSet('all', 'changed')]
    [string]$Scope = 'all',

    [int]$MaxParallel = 0,

    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'

$clangTidySkippedFiles = @(
    # clang-tidy is intentionally skipped for snapshot_dump.cpp because isolated timings show
    # that Clang diagnostics, clang-analyzer, and misc-include-cleaner all go pathological on
    # this translation unit, turning the optional tidy sweep into a multi-minute stall.
    'src/diagnostics/snapshot_dump.cpp'
)

$clangTidyIgnoredUnusedIncludeWarnings = @(
    # The active include-cleaner build does not model these Win32 interface headers correctly.
    'src/display/display_config.cpp|shobjidl.h',
    'src/util/resource_loader.cpp|windows.h',
    'src/util/utf8.cpp|windows.h'
)

function ConvertTo-RepoSlashPath {
    param(
        [string]$Path
    )

    return $Path -replace '\\', '/'
}

function Test-ClangTidySkippedFile {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    $relativePath = ConvertTo-RepoSlashPath -Path (Get-RelativeRepoPath -RepoRoot $RepoRoot -FullPath $FullPath)
    return $relativePath -in $clangTidySkippedFiles
}

function Test-IgnoredUnusedIncludeWarning {
    param(
        [string]$RepoRoot,
        [string]$Text
    )

    $match = [regex]::Match(
        $Text,
        '^(?<file>.*\.(?:cpp|h)):\d+:\d+: warning: included header (?<header>\S+) is not used directly \[misc-include-cleaner\]$')
    if (-not $match.Success) {
        return $false
    }

    $relativePath = ConvertTo-RepoSlashPath -Path (Get-RelativeRepoPath -RepoRoot $RepoRoot -FullPath $match.Groups['file'].Value)
    $key = "$relativePath|$($match.Groups['header'].Value)"
    return $key -in $clangTidyIgnoredUnusedIncludeWarnings
}

function Test-TidyStateReady {
    param(
        [hashtable]$State,
        [int]$TimeoutSeconds
    )

    if ($State.Process.HasExited) {
        return $true
    }

    return ([System.DateTime]::UtcNow - $State.StartedAt).TotalSeconds -ge $TimeoutSeconds
}

function Resolve-ClangTidyPath {
    param(
        [string]$RepoRoot,
        [string]$CompileCommandsPath
    )

    function Add-Candidate {
        param(
            [System.Collections.Generic.List[string]]$List,
            [string]$Candidate
        )

        if (-not [string]::IsNullOrWhiteSpace($Candidate) -and -not $List.Contains($Candidate)) {
            $List.Add($Candidate)
        }
    }

    function Add-VsLlvmCandidates {
        param(
            [System.Collections.Generic.List[string]]$List,
            [string]$VsRoot
        )

        if ([string]::IsNullOrWhiteSpace($VsRoot)) {
            return
        }

        Add-Candidate -List $List -Candidate (Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin\clang-tidy.exe')
        Add-Candidate -List $List -Candidate (Join-Path $VsRoot 'VC\Tools\Llvm\bin\clang-tidy.exe')
    }

    function Resolve-ClangTidyFromBuildDatabase {
        param(
            [string]$DatabasePath
        )

        if (-not (Test-Path -LiteralPath $DatabasePath)) {
            return $null
        }

        try {
            $database = Get-Content -LiteralPath $DatabasePath -Raw | ConvertFrom-Json
        } catch {
            return $null
        }

        foreach ($entry in $database) {
            if (-not $entry.command) {
                continue
            }

            $match = [regex]::Match($entry.command, '^(?:"(?<quoted>[^"]*cl\.exe)"|(?<plain>\S*cl\.exe))', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
            if (-not $match.Success) {
                continue
            }

            $compilerPath = $match.Groups['quoted'].Value
            if ([string]::IsNullOrWhiteSpace($compilerPath)) {
                $compilerPath = $match.Groups['plain'].Value
            }

            if ([string]::IsNullOrWhiteSpace($compilerPath)) {
                continue
            }

            $msvcMarker = '\VC\Tools\MSVC\'
            $markerIndex = $compilerPath.IndexOf($msvcMarker, [System.StringComparison]::OrdinalIgnoreCase)
            if ($markerIndex -lt 0) {
                continue
            }

            $vsRoot = $compilerPath.Substring(0, $markerIndex)
            $candidate = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clang-tidy.exe'
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }

            $candidate = Join-Path $vsRoot 'VC\Tools\Llvm\bin\clang-tidy.exe'
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }

        return $null
    }

    $candidates = [System.Collections.Generic.List[string]]::new()

    Add-VsLlvmCandidates -List $candidates -VsRoot $env:VSINSTALLDIR

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command clang-tidy.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $devenvCmd = Join-Path $RepoRoot 'devenv.cmd'
    if (Test-Path -LiteralPath $devenvCmd) {
        $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clang-tidy.exe" 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $probe) {
            return $probe.Trim()
        }
    }

    $buildDatabaseCandidate = Resolve-ClangTidyFromBuildDatabase -DatabasePath $CompileCommandsPath
    if ($buildDatabaseCandidate) {
        return $buildDatabaseCandidate
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

function Get-TrackedCppFiles {
    param(
        [string]$RepoRoot
    )

    $searchRoots = @(
        Join-Path $RepoRoot 'src'
        Join-Path $RepoRoot 'tests'
    )

    $files = foreach ($searchRoot in $searchRoots) {
        if (-not (Test-Path -LiteralPath $searchRoot)) {
            continue
        }

        Get-ChildItem -LiteralPath $searchRoot -Recurse -File -Filter *.cpp |
            Where-Object {
                $_.FullName -notmatch '[\\/]vendor[\\/]' -and
                $_.Name -ne 'board_gigabyte_siv.cpp' -and
                -not (Test-ClangTidySkippedFile -RepoRoot $RepoRoot -FullPath $_.FullName)
            }
    }

    return $files | Sort-Object FullName
}

function Get-ChangedCppFiles {
    param(
        [string]$RepoRoot
    )

    $repoRootSlash = $RepoRoot -replace '\\', '/'
    $pathSpecs = @('src/**/*.cpp', 'tests/**/*.cpp')

    $changedFiles = [System.Collections.Generic.List[string]]::new()

    $headExists = $false
    & git -C $RepoRoot rev-parse --verify HEAD *> $null
    if ($LASTEXITCODE -eq 0) {
        $headExists = $true
    }

    if ($headExists) {
        $gitDiffOutput = & git -C $RepoRoot diff --name-only --diff-filter=ACMR HEAD -- $pathSpecs 2>$null
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    } else {
        $gitDiffOutput = & git -C $RepoRoot diff --name-only --diff-filter=ACMR --cached -- $pathSpecs 2>$null
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    }

    $untrackedOutput = & git -C $RepoRoot ls-files --others --exclude-standard -- $pathSpecs 2>$null
    foreach ($entry in $untrackedOutput) {
        if (-not [string]::IsNullOrWhiteSpace($entry)) {
            $changedFiles.Add($entry.Trim())
        }
    }

    $files = foreach ($relativePath in ($changedFiles | Sort-Object -Unique)) {
        $fullPath = Join-Path $RepoRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath)) {
            continue
        }

        $normalizedPath = [System.IO.Path]::GetFullPath($fullPath)
        if ($normalizedPath -notmatch '[\\/]src[\\/]|[\\/]tests[\\/]') {
            continue
        }
        if ($normalizedPath -match '[\\/]vendor[\\/]') {
            continue
        }
        if ([System.IO.Path]::GetFileName($normalizedPath) -eq 'board_gigabyte_siv.cpp') {
            continue
        }
        if (Test-ClangTidySkippedFile -RepoRoot $RepoRoot -FullPath $normalizedPath) {
            continue
        }

        Get-Item -LiteralPath $normalizedPath
    }

    return $files | Sort-Object FullName
}

function New-ReportWriter {
    param(
        [string]$PreferredPath
    )

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)

    try {
        $writer = [System.IO.StreamWriter]::new($PreferredPath, $false, $utf8NoBom)
        return @{
            Path = $PreferredPath
            Writer = $writer
            UsedFallback = $false
        }
    } catch {
        $directory = Split-Path -Path $PreferredPath -Parent
        $fallbackPath = Join-Path $directory ("clang_tidy_report_{0}_{1}.txt" -f (Get-Date -Format 'yyyyMMdd_HHmmss'), $PID)
        $writer = [System.IO.StreamWriter]::new($fallbackPath, $false, $utf8NoBom)
        return @{
            Path = $fallbackPath
            Writer = $writer
            UsedFallback = $true
        }
    }
}

function Get-RelativeRepoPath {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    $normalizedRoot = [System.IO.Path]::GetFullPath($RepoRoot)
    if (-not $normalizedRoot.EndsWith('\')) {
        $normalizedRoot += '\'
    }

    $normalizedPath = [System.IO.Path]::GetFullPath($FullPath)
    if ($normalizedPath.StartsWith($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $normalizedPath.Substring($normalizedRoot.Length)
    }

    return $normalizedPath
}

function Start-TidyProcess {
    param(
        [string]$ClangTidyPath,
        [string]$RepoRoot,
        [string]$RelativePath,
        [string]$Mode,
        [int]$Index
    )

    $tempLogRoot = Join-Path $RepoRoot 'build\clang_tidy_units'
    if (-not (Test-Path -LiteralPath $tempLogRoot)) {
        New-Item -ItemType Directory -Path $tempLogRoot -Force | Out-Null
    }

    $logBase = Join-Path $tempLogRoot ("clang_tidy_unit_{0:D4}_{1}" -f $Index, [System.Guid]::NewGuid().ToString('N'))
    $stdoutPath = "$logBase.stdout.log"
    $stderrPath = "$logBase.stderr.log"

    $commandArgs = @(
        '--quiet',
        '-p', (Join-Path $RepoRoot 'build\cmake')
    )
    if ($Mode -eq 'fix') {
        $commandArgs += @('-fix', '--format-style=none')
    }
    $commandArgs += $RelativePath

    $argumentString = ($commandArgs | ForEach-Object {
            if ($_ -match '[\s"]') {
                '"' + ($_ -replace '"', '\"') + '"'
            } else {
                $_
            }
        }) -join ' '

    $processStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $processStartInfo.FileName = $ClangTidyPath
    $processStartInfo.Arguments = $argumentString
    $processStartInfo.WorkingDirectory = $RepoRoot
    $processStartInfo.UseShellExecute = $false
    $processStartInfo.CreateNoWindow = $true
    $processStartInfo.RedirectStandardOutput = $true
    $processStartInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $processStartInfo
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    return @{
        Index = $Index
        RelativePath = $RelativePath
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
        StdoutTask = $stdoutTask
        StderrTask = $stderrTask
        Process = $process
        StartedAt = [System.DateTime]::UtcNow
    }
}

function Complete-TidyProcess {
    param(
        [hashtable]$State,
        [int]$TimeoutSeconds,
        [string]$RepoRoot
    )

    $timedOut = $false
    $elapsedMilliseconds = ([System.DateTime]::UtcNow - $state.StartedAt).TotalMilliseconds
    $remainingMilliseconds = [Math]::Max(0, [int](($TimeoutSeconds * 1000) - $elapsedMilliseconds))
    if (-not $state.Process.HasExited -and -not $state.Process.WaitForExit($remainingMilliseconds)) {
        $timedOut = $true
        Stop-Process -Id $state.Process.Id -Force -ErrorAction SilentlyContinue
        $null = $state.Process.WaitForExit(5000)
    }

    $outputLines = @()
    if ($null -ne $state.StdoutTask) {
        [void]$state.StdoutTask.Wait(5000)
        $stdoutText = if ($state.StdoutTask.IsCompleted) { $state.StdoutTask.Result } else { '' }
        if (-not [string]::IsNullOrEmpty($stdoutText)) {
            $outputLines += $stdoutText -split '\r?\n'
        }
    } elseif (Test-Path -LiteralPath $state.StdoutPath) {
        $outputLines += Get-Content -LiteralPath $state.StdoutPath
        Remove-Item -LiteralPath $state.StdoutPath -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $state.StderrTask) {
        [void]$state.StderrTask.Wait(5000)
        $stderrText = if ($state.StderrTask.IsCompleted) { $state.StderrTask.Result } else { '' }
        if (-not [string]::IsNullOrEmpty($stderrText)) {
            $outputLines += $stderrText -split '\r?\n'
        }
    } elseif (Test-Path -LiteralPath $state.StderrPath) {
        $outputLines += Get-Content -LiteralPath $state.StderrPath
        Remove-Item -LiteralPath $state.StderrPath -Force -ErrorAction SilentlyContinue
    }

    $filteredOutputLines = [System.Collections.Generic.List[string]]::new()
    $skipIncludeCleanerMissingBlock = $false
    $skipIgnoredUnusedIncludeBlock = $false
    foreach ($line in $outputLines) {
        $text = if ($null -eq $line) { '' } else { $line.ToString() }

        if ($skipIgnoredUnusedIncludeBlock) {
            if ($text -match '^\s' -or [string]::IsNullOrWhiteSpace($text)) {
                continue
            }
            $skipIgnoredUnusedIncludeBlock = $false
        }

        if ($skipIncludeCleanerMissingBlock) {
            if ($text -match '^\s' -or [string]::IsNullOrWhiteSpace($text)) {
                continue
            }
            $skipIncludeCleanerMissingBlock = $false
        }

        if ($text -match 'warning: no header providing ".+" is directly included \[misc-include-cleaner\]') {
            $skipIncludeCleanerMissingBlock = $true
            continue
        }

        if (Test-IgnoredUnusedIncludeWarning -RepoRoot $RepoRoot -Text $text) {
            $skipIgnoredUnusedIncludeBlock = $true
            continue
        }

        if ($text -match '^\d+ warnings generated\.$') {
            continue
        }

        $filteredOutputLines.Add($text)
    }

    $exitCode = $state.Process.ExitCode
    if ($timedOut) {
        $exitCode = -1
    }
    $state.Process.Dispose()

    return @{
        Index = $state.Index
        RelativePath = $state.RelativePath
        ExitCode = $exitCode
        OutputLines = @($filteredOutputLines)
        Duration = ([System.DateTime]::UtcNow - $state.StartedAt)
        TimedOut = $timedOut
    }
}

function Write-TidyResult {
    param(
        [System.IO.StreamWriter]$Writer,
        [hashtable]$Result,
        [int]$FileCount,
        [int]$TimeoutSeconds,
        [ref]$TidyFailed
    )

    $displayIndex = $Result.Index + 1
    $durationText = '{0:N2}' -f $Result.Duration.TotalSeconds
    if ($Result.TimedOut) {
        Write-Host "[$displayIndex/$FileCount] timed out $($Result.RelativePath) after ${durationText}s"
    } else {
        Write-Host "[$displayIndex/$FileCount] completed $($Result.RelativePath) in ${durationText}s"
    }

    $Writer.WriteLine('===============================================================================')
    $Writer.WriteLine("[$displayIndex/$FileCount] $($Result.RelativePath)")
    $Writer.WriteLine("Elapsed seconds: $durationText")
    if ($Result.TimedOut) {
        $Writer.WriteLine("Timed out after $TimeoutSeconds seconds")
    }

    foreach ($entry in $Result.OutputLines) {
        if ($null -eq $entry) {
            $Writer.WriteLine()
        } else {
            $Writer.WriteLine($entry.ToString())
        }
    }

    if ($Result.ExitCode -ne 0) {
        $TidyFailed.Value = $true
        $Writer.WriteLine("clang-tidy exit code: $($Result.ExitCode)")
    }

    $Writer.WriteLine()
    $Writer.Flush()
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$compileCommands = Join-Path $resolvedRoot 'build\cmake\compile_commands.json'
$preferredReportPath = Join-Path $resolvedRoot 'build\clang_tidy_report.txt'
$clangTidy = Resolve-ClangTidyPath -RepoRoot $resolvedRoot -CompileCommandsPath $compileCommands

if (-not $clangTidy) {
    Write-Host 'clang-tidy.exe was not found. Install the Visual Studio LLVM tools or add clang-tidy to PATH.'
    exit 1
}

if (-not (Test-Path -LiteralPath $compileCommands)) {
    Write-Host 'build\cmake\compile_commands.json was not found. Build the project first with build.cmd.'
    exit 1
}

$files = if ($Scope -eq 'changed') {
    @(Get-ChangedCppFiles -RepoRoot $resolvedRoot)
} else {
    @(Get-TrackedCppFiles -RepoRoot $resolvedRoot)
}
if ($files.Count -eq 0) {
    if ($Scope -eq 'changed') {
        Write-Host 'No eligible changed project translation units were found.'
        exit 0
    }

    Write-Host 'No eligible project translation units were found.'
    exit 1
}

$parallelism = $MaxParallel
if ($parallelism -le 0) {
    $parallelism = [Math]::Min([Math]::Max([Environment]::ProcessorCount - 1, 1), 8)
}
$parallelism = [Math]::Min([Math]::Max($parallelism, 1), $files.Count)

$report = New-ReportWriter -PreferredPath $preferredReportPath
$reportPath = $report.Path
$writer = $report.Writer
try {
    $writer.WriteLine('clang-tidy report')
    $writer.WriteLine("Mode: $Mode")
    $writer.WriteLine("Scope: $Scope")
    $writer.WriteLine("Max parallel: $parallelism")
    $writer.WriteLine("Timeout seconds: $TimeoutSeconds")
    $writer.WriteLine("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')")
    $writer.WriteLine("Using clang-tidy: $clangTidy")
    $writer.WriteLine("Build database: $compileCommands")
    $writer.WriteLine()

    Write-Host "Using clang-tidy: $clangTidy"
    Write-Host "Running optional clang-tidy sweep for $($files.Count) translation units with parallelism=$parallelism..."
    if ($report.UsedFallback) {
        Write-Host "Primary report path is locked; writing to $reportPath instead."
    }
    Write-Host "Writing full clang-tidy output to $reportPath"

    $tidyFailed = $false
    $active = [System.Collections.Generic.List[object]]::new()

    for ($index = 0; $index -lt $files.Count; $index++) {
        $file = $files[$index]
        $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName

        Write-Host "[$($index + 1)/$($files.Count)] queued $relativePath"
        $active.Add((Start-TidyProcess -ClangTidyPath $clangTidy -RepoRoot $resolvedRoot -RelativePath $relativePath -Mode $Mode -Index $index))

        while ($active.Count -ge $parallelism) {
            $finishedState = $null
            foreach ($state in @($active)) {
                if (Test-TidyStateReady -State $state -TimeoutSeconds $TimeoutSeconds) {
                    $finishedState = $state
                    break
                }
            }

            if ($null -eq $finishedState) {
                Start-Sleep -Milliseconds 200
                continue
            }

            [void]$active.Remove($finishedState)
            $result = Complete-TidyProcess -State $finishedState -TimeoutSeconds $TimeoutSeconds -RepoRoot $resolvedRoot
            Write-TidyResult -Writer $writer -Result $result -FileCount $files.Count -TimeoutSeconds $TimeoutSeconds -TidyFailed ([ref]$tidyFailed)
        }
    }

    while ($active.Count -gt 0) {
        $finishedState = $null
        foreach ($state in @($active)) {
            if (Test-TidyStateReady -State $state -TimeoutSeconds $TimeoutSeconds) {
                $finishedState = $state
                break
            }
        }

        if ($null -eq $finishedState) {
            Start-Sleep -Milliseconds 200
            continue
        }

        [void]$active.Remove($finishedState)
        $result = Complete-TidyProcess -State $finishedState -TimeoutSeconds $TimeoutSeconds -RepoRoot $resolvedRoot
        Write-TidyResult -Writer $writer -Result $result -FileCount $files.Count -TimeoutSeconds $TimeoutSeconds -TidyFailed ([ref]$tidyFailed)
    }

    if ($tidyFailed) {
        if ($Mode -eq 'fix') {
            Write-Host 'clang-tidy fix completed with remaining issues.'
        } else {
            Write-Host 'clang-tidy failed.'
        }
        Write-Host "Full clang-tidy report: $reportPath"
        exit 1
    }

    if ($Mode -eq 'fix') {
        Write-Host 'clang-tidy fixes completed.'
    } else {
        Write-Host 'clang-tidy passed.'
    }
    Write-Host "Full clang-tidy report: $reportPath"
    exit 0
} finally {
    $writer.Dispose()
}
