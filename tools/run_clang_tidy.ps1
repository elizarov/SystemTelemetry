param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('check', 'fix')]
    [string]$Mode = 'check',

    [ValidateSet('all', 'changed')]
    [string]$Scope = 'all',

    [ValidateSet('full', 'includes')]
    [string]$CheckSet = 'full',

    [int]$MaxParallel = 0,

    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'

$clangTidyIgnoredUnusedIncludeWarnings = @(
    # The active include-cleaner build does not model these Win32 interface headers correctly.
    'src/dashboard/constants.h|windows.h',
    'src/dashboard/dashboard_app.h|windows.h',
    'src/dashboard/dashboard_menu_types.h|windows.h',
    'src/dashboard/dashboard_shell_ui.h|windows.h',
    'src/dashboard_renderer/dashboard_renderer.h|windows.h',
    'src/diagnostics/app_icon_export.cpp|windows.h',
    'src/diagnostics/crash_report.cpp|windows.h',
    'src/diagnostics/crash_report.cpp|dbghelp.h',
    'src/diagnostics/diagnostics.h|windows.h',
    'src/display/constants.h|windows.h',
    'src/display/display_config.h|windows.h',
    'src/display/display_config.cpp|shobjidl.h',
    'src/display/monitor.h|windows.h',
    'src/layout_edit/layout_edit_controller.h|windows.h',
    'src/layout_edit_dialog/impl/dialog_proc.h|windows.h',
    'src/layout_edit_dialog/impl/state.h|windows.h',
    'src/layout_edit_dialog/impl/trace.h|windows.h',
    'src/layout_edit_dialog/impl/util.h|windows.h',
    'src/layout_edit_dialog/layout_edit_dialog.h|windows.h',
    'src/layout_edit_dialog/theme_preview.h|windows.h',
    'src/config/config_io.h|windows.h',
    'src/dashboard/autostart.h|windows.h',
    'src/dashboard/fps_service.h|windows.h',
    'src/main/main.cpp|windows.h',
    'src/renderer/png_export.cpp|windows.h',
    'src/renderer/impl/d2d_renderer.h|windows.h',
    'src/renderer/impl/d2d_renderer.h|dwrite.h',
    'src/renderer/renderer.h|windows.h',
    'src/util/elevated_process.h|windows.h',
    'src/telemetry/board/asus/board_asus_armoury_crate.cpp|windows.h',
    'src/telemetry/board/msi/board_msi_center.cpp|windows.h',
    'src/telemetry/fps/fps_etw_provider.cpp|windows.h',
    'src/telemetry/gpu/gpu_vendor.cpp|windows.h',
    'src/telemetry/gpu/intel/gpu_intel_level_zero.cpp|windows.h',
    'src/telemetry/gpu/nvidia/gpu_nvidia_nvml.cpp|windows.h',
    'src/telemetry/impl/collector_state.h|winsock2.h',
    'src/telemetry/impl/collector_state.h|ws2tcpip.h',
    'src/telemetry/impl/collector_state.h|windows.h',
    'src/telemetry/impl/collector_state.h|dxgi.h',
    'src/telemetry/impl/collector_state.h|iphlpapi.h',
    'src/telemetry/impl/collector_state.h|netioapi.h',
    'src/telemetry/impl/collector_state.h|pdhmsg.h',
    'src/telemetry/impl/collector_storage_selection.h|windows.h',
    'src/telemetry/impl/system_info_support.h|windows.h',
    'src/telemetry/telemetry.h|windows.h',
    'src/util/file_path.cpp|windows.h',
    'src/util/command_line.cpp|windows.h',
    'src/util/high_precision_timer.cpp|windows.h',
    'src/util/message_box.cpp|windows.h',
    'src/util/scale.h|windows.h',
    'src/util/lightweight_mutex.cpp|windows.h',
    'src/util/strings.h|windows.h',
    'src/util/temp_file.cpp|windows.h',
    'src/util/trace.cpp|windows.h',
    'src/util/win32_format.cpp|windows.h',
    # These headers expose declarations through project macros or umbrella types that include-cleaner cannot map.
    'src/dashboard/dashboard_app.h|constants.h',
    'src/dashboard/dashboard_shell_ui.h|dashboard_menu_types.h',
    'src/diagnostics/diagnostics.h|snapshot_dump.h',
    'src/display/display_config.h|snapshot_dump.h',
    'src/display/monitor.h|scale.h',
    'src/layout_edit/layout_edit_parameter_edit.h|layout_edit_parameter_metadata.h',
    'src/util/resource_strings.h|resource_strings.generated.h',
    'src/util/resource_loader.cpp|windows.h',
    'src/util/utf8.cpp|windows.h',
    'src/telemetry/fps/fps_service_client_provider.cpp|windows.h'
)

$clangTidyIgnoredUnusedIncludeWarningSet = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($warning in $clangTidyIgnoredUnusedIncludeWarnings) {
    [void]$clangTidyIgnoredUnusedIncludeWarningSet.Add($warning)
}

function ConvertTo-RepoSlashPath {
    param(
        [string]$Path
    )

    return $Path -replace '\\', '/'
}

function Get-HeaderKeyLeaf {
    param(
        [string]$Header
    )

    $normalizedHeader = ConvertTo-RepoSlashPath -Path $Header
    $parts = $normalizedHeader -split '/'
    return $parts[$parts.Count - 1]
}

function Test-IgnoredUnusedIncludeKey {
    param(
        [string]$RelativePath,
        [string]$Header
    )

    $relativeSlashPath = ConvertTo-RepoSlashPath -Path $RelativePath
    $normalizedHeader = ConvertTo-RepoSlashPath -Path $Header
    if ($clangTidyIgnoredUnusedIncludeWarningSet.Contains("$relativeSlashPath|$normalizedHeader")) {
        return $true
    }

    $headerLeaf = Get-HeaderKeyLeaf -Header $normalizedHeader
    return $clangTidyIgnoredUnusedIncludeWarningSet.Contains("$relativeSlashPath|$headerLeaf")
}

function Test-IgnoredUnusedIncludeDiagnostic {
    param(
        [string]$RepoRoot,
        [string]$Text
    )

    $match = [regex]::Match(
        $Text,
        '^(?<file>.*\.(?:cpp|h)):\d+:\d+: (?:warning|error): included header (?<header>\S+) is not used directly \[misc-include-cleaner(?:,-warnings-as-errors)?\]$')
    if (-not $match.Success) {
        return $false
    }

    $relativePath = ConvertTo-RepoSlashPath -Path (Get-RelativeRepoPath -RepoRoot $RepoRoot -FullPath $match.Groups['file'].Value)
    return Test-IgnoredUnusedIncludeKey -RelativePath $relativePath -Header $match.Groups['header'].Value
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
    if ($env:GITHUB_ACTIONS -ne 'true' -and (Test-Path -LiteralPath $devenvCmd)) {
        $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clang-tidy.exe" 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $probe) {
            return $probe.Trim()
        }
    }

    foreach ($pattern in @(
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-tidy.exe'
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\bin\clang-tidy.exe'
    )) {
        $discovered = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($discovered) {
            return $discovered.FullName
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

function Test-EligibleTidyFile {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    $normalizedPath = [System.IO.Path]::GetFullPath($FullPath)
    $extension = [System.IO.Path]::GetExtension($normalizedPath)
    if ($extension -ne '.cpp' -and $extension -ne '.h') {
        return $false
    }
    if ($normalizedPath -notmatch '[\\/]src[\\/]|[\\/]tests[\\/]') {
        return $false
    }
    if ($normalizedPath -match '[\\/]vendor[\\/]') {
        return $false
    }
    $fileName = [System.IO.Path]::GetFileName($normalizedPath)
    if ($fileName -eq 'config_desc.h') {
        return $false
    }
    if ($fileName -eq 'board_gigabyte_siv.cpp' -or $fileName -eq 'board_gigabyte_siv_bridge.cpp' -or
        $fileName -eq 'board_msi_center_bridge.cpp') {
        return $false
    }
    return $true
}

function Get-TrackedTidyFiles {
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

        Get-ChildItem -LiteralPath $searchRoot -Recurse -File |
            Where-Object { Test-EligibleTidyFile -RepoRoot $RepoRoot -FullPath $_.FullName }
    }

    return $files | Sort-Object FullName
}

function Invoke-GitOutput {
    param(
        [string]$RepoRoot,
        [string[]]$Arguments
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $hasNativeCommandPreference = Test-Path -Path variable:PSNativeCommandUseErrorActionPreference
    if ($hasNativeCommandPreference) {
        $previousNativeCommandPreference = $PSNativeCommandUseErrorActionPreference
    }

    try {
        $ErrorActionPreference = 'Continue'
        if ($hasNativeCommandPreference) {
            $PSNativeCommandUseErrorActionPreference = $false
        }
        $output = & git -C $RepoRoot @Arguments 2>$null
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($hasNativeCommandPreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeCommandPreference
        }
    }

    if ($exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $exitCode."
    }

    return $output
}

function Get-ChangedTidyFiles {
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
        $gitDiffOutput = Invoke-GitOutput -RepoRoot $RepoRoot -Arguments @(
            'diff'
            '--name-only'
            '--diff-filter=ACMR'
            'HEAD'
            '--'
            $pathSpecs
        )
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    } else {
        $gitDiffOutput = Invoke-GitOutput -RepoRoot $RepoRoot -Arguments @(
            'diff'
            '--name-only'
            '--diff-filter=ACMR'
            '--cached'
            '--'
            $pathSpecs
        )
        foreach ($entry in $gitDiffOutput) {
            if (-not [string]::IsNullOrWhiteSpace($entry)) {
                $changedFiles.Add($entry.Trim())
            }
        }
    }

    $untrackedOutput = Invoke-GitOutput -RepoRoot $RepoRoot -Arguments @(
        'ls-files'
        '--others'
        '--exclude-standard'
        '--'
        $pathSpecs
    )
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
        if (-not (Test-EligibleTidyFile -RepoRoot $RepoRoot -FullPath $normalizedPath)) {
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
        $reportBaseName = [System.IO.Path]::GetFileNameWithoutExtension($PreferredPath)
        $fallbackPath = Join-Path $directory ("{0}_{1}_{2}.txt" -f $reportBaseName, (Get-Date -Format 'yyyyMMdd_HHmmss'), $PID)
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

function Get-IncludeCleanerLineFilter {
    param(
        [string]$RepoRoot,
        [string]$RelativePath
    )

    $fullPath = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        return $null
    }

    $relativeSlashPath = ConvertTo-RepoSlashPath -Path $RelativePath
    $ranges = [System.Collections.Generic.List[string]]::new()
    $lineNumber = 0
    foreach ($line in [System.IO.File]::ReadLines($fullPath)) {
        $lineNumber++
        $match = [regex]::Match($line, '^\s*#\s*include\s*[<"](?<header>[^>"]+)[>"]')
        if (-not $match.Success) {
            continue
        }

        $header = ConvertTo-RepoSlashPath -Path $match.Groups['header'].Value
        if (Test-IgnoredUnusedIncludeKey -RelativePath $relativeSlashPath -Header $header) {
            continue
        }

        $ranges.Add("[$lineNumber,$lineNumber]")
    }

    if ($ranges.Count -eq 0) {
        return $null
    }

    $lineFilterPath = $RelativePath -replace '/', '\'
    $escapedPath = $lineFilterPath.Replace('\', '\\').Replace('"', '\"')
    return '[{"name":"' + $escapedPath + '","lines":[' + ($ranges -join ',') + ']}]'
}

function Start-TidyProcess {
    param(
        [string]$ClangTidyPath,
        [string]$RepoRoot,
        [string]$RelativePath,
        [string]$Mode,
        [string]$CheckSet,
        [string]$LineFilter,
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

    if ($CheckSet -eq 'includes') {
        $commandArgs += @(
            '-checks=-*,misc-include-cleaner',
            '-warnings-as-errors=misc-include-cleaner'
        )
        if (-not [string]::IsNullOrWhiteSpace($LineFilter)) {
            $commandArgs += "--line-filter=$LineFilter"
        }
    } else {
        if ([System.IO.Path]::GetExtension($RelativePath) -eq '.h') {
            $commandArgs += '--extra-arg=-Wunused-function'
        } else {
            $commandArgs += '--extra-arg=/clang:-Wunused-function'
        }
    }

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

        if ($text -match '(?:warning|error): no header providing ".+" is directly included \[misc-include-cleaner(?:,-warnings-as-errors)?\]') {
            $skipIncludeCleanerMissingBlock = $true
            continue
        }

        if (Test-IgnoredUnusedIncludeDiagnostic -RepoRoot $RepoRoot -Text $text) {
            $skipIgnoredUnusedIncludeBlock = $true
            continue
        }

        if ([string]::IsNullOrWhiteSpace($text)) {
            continue
        }

        if ($text -match '^\d+ warnings? generated\.$') {
            continue
        }

        if ($text -match '^\d+ warnings? and \d+ errors? generated\.$') {
            continue
        }

        if ($text -match '^Error while processing .+\.$') {
            continue
        }

        if ($text -match '^\[\d+/\d+\] \(\d+/\d+\) Processing file .+\.$') {
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
        HasReportableOutput = ($filteredOutputLines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1) -ne $null
        Duration = ([System.DateTime]::UtcNow - $state.StartedAt)
        TimedOut = $timedOut
    }
}

function Test-IncludeFixOutputSucceeded {
    param(
        [string[]]$Lines
    )

    $unusedIncludeDiagnostics = 0
    $appliedFixes = -1
    $totalFixes = -1

    foreach ($entry in $Lines) {
        $text = if ($null -eq $entry) { '' } else { $entry.ToString() }
        if ([string]::IsNullOrWhiteSpace($text)) {
            continue
        }
        if ($text -match '^(?<file>.*\.(?:cpp|h)):\d+:\d+: (?:warning|error): included header (?<header>\S+) is not used directly \[misc-include-cleaner(?:,-warnings-as-errors)?\]$') {
            $unusedIncludeDiagnostics++
            continue
        }
        if ($text -match '^\s*\d+\s*\|') {
            continue
        }
        if ($text -match '^\s*\|') {
            continue
        }
        if ($text -match ':\d+:\d+: note: FIX-IT applied suggested code changes$') {
            continue
        }

        $summary = [regex]::Match($text, '^clang-tidy applied (?<applied>\d+) of (?<total>\d+) suggested fixes\.$')
        if ($summary.Success) {
            $appliedFixes = [int]$summary.Groups['applied'].Value
            $totalFixes = [int]$summary.Groups['total'].Value
            continue
        }

        return $false
    }

    return $unusedIncludeDiagnostics -gt 0 -and $totalFixes -gt 0 -and $appliedFixes -eq $totalFixes
}

function Write-TidyResult {
    param(
        [System.IO.StreamWriter]$Writer,
        [hashtable]$Result,
        [int]$FileCount,
        [int]$TimeoutSeconds,
        [string]$Mode,
        [string]$CheckSet,
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

    $includeFixSucceeded = $false
    if ($Mode -eq 'fix' -and $CheckSet -eq 'includes' -and -not $Result.TimedOut) {
        $includeFixSucceeded = Test-IncludeFixOutputSucceeded -Lines $Result.OutputLines
    }

    if ($Result.ExitCode -ne 0 -and ($Result.TimedOut -or $Result.HasReportableOutput) -and -not $includeFixSucceeded) {
        $TidyFailed.Value = $true
        $Writer.WriteLine("clang-tidy exit code: $($Result.ExitCode)")
    } elseif ($Result.ExitCode -ne 0 -and $includeFixSucceeded) {
        $Writer.WriteLine("clang-tidy exit code ignored after applying unused include fixes: $($Result.ExitCode)")
    } elseif ($Result.ExitCode -ne 0) {
        $Writer.WriteLine("clang-tidy exit code ignored after filtering: $($Result.ExitCode)")
    }

    $Writer.WriteLine()
    $Writer.Flush()
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$compileCommands = Join-Path $resolvedRoot 'build\cmake\compile_commands.json'
$reportFileName = if ($CheckSet -eq 'includes') { 'clang_include_cleaner_report.txt' } else { 'clang_tidy_report.txt' }
$preferredReportPath = Join-Path $resolvedRoot "build\$reportFileName"
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
    @(Get-ChangedTidyFiles -RepoRoot $resolvedRoot)
} else {
    @(Get-TrackedTidyFiles -RepoRoot $resolvedRoot)
}

if ($files.Count -eq 0) {
    if ($Scope -eq 'changed') {
        Write-Host 'No eligible changed project source or header files were found.'
        exit 0
    }

    Write-Host 'No eligible project source or header files were found.'
    exit 1
}

$lineFiltersByPath = @{}
if ($CheckSet -eq 'includes') {
    $includeFiles = [System.Collections.Generic.List[object]]::new()
    foreach ($file in $files) {
        $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName
        $lineFilter = Get-IncludeCleanerLineFilter -RepoRoot $resolvedRoot -RelativePath $relativePath
        if ([string]::IsNullOrWhiteSpace($lineFilter)) {
            continue
        }

        $lineFiltersByPath[$relativePath] = $lineFilter
        $includeFiles.Add($file)
    }

    $files = @($includeFiles.ToArray())
    if ($files.Count -eq 0) {
        if ($Scope -eq 'changed') {
            Write-Host 'No eligible changed project source or header files with non-ignored include lines were found.'
        } else {
            Write-Host 'No eligible project source or header files with non-ignored include lines were found.'
        }
        exit 0
    }
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
    $writer.WriteLine("Check set: $CheckSet")
    $writer.WriteLine("Max parallel: $parallelism")
    $writer.WriteLine("Timeout seconds: $TimeoutSeconds")
    $writer.WriteLine("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')")
    $writer.WriteLine("Using clang-tidy: $clangTidy")
    $writer.WriteLine("Build database: $compileCommands")
    if ($CheckSet -eq 'includes') {
        $writer.WriteLine('Line filter: non-ignored include directives only')
    }
    $writer.WriteLine()

    Write-Host "Using clang-tidy: $clangTidy"
    if ($CheckSet -eq 'includes') {
        Write-Host "Running clang-tidy include-cleaner unused-include sweep for $($files.Count) source and header files with parallelism=$parallelism..."
    } else {
        Write-Host "Running optional clang-tidy sweep for $($files.Count) source and header files with parallelism=$parallelism..."
    }
    if ($report.UsedFallback) {
        Write-Host "Primary report path is locked; writing to $reportPath instead."
    }
    Write-Host "Writing full clang-tidy output to $reportPath"

    $tidyFailed = $false
    $active = [System.Collections.Generic.List[object]]::new()

    for ($index = 0; $index -lt $files.Count; $index++) {
        $file = $files[$index]
        $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName
        $lineFilter = if ($lineFiltersByPath.ContainsKey($relativePath)) { $lineFiltersByPath[$relativePath] } else { $null }

        Write-Host "[$($index + 1)/$($files.Count)] queued $relativePath"
        $active.Add((Start-TidyProcess -ClangTidyPath $clangTidy -RepoRoot $resolvedRoot -RelativePath $relativePath -Mode $Mode -CheckSet $CheckSet -LineFilter $lineFilter -Index $index))

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
            Write-TidyResult -Writer $writer -Result $result -FileCount $files.Count -TimeoutSeconds $TimeoutSeconds -Mode $Mode -CheckSet $CheckSet -TidyFailed ([ref]$tidyFailed)
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
        Write-TidyResult -Writer $writer -Result $result -FileCount $files.Count -TimeoutSeconds $TimeoutSeconds -Mode $Mode -CheckSet $CheckSet -TidyFailed ([ref]$tidyFailed)
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
