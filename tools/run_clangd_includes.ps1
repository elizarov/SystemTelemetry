param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('all', 'changed')]
    [string]$Scope = 'all',

    [int]$MaxParallel = 0,

    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'

$ignoredUnusedIncludeWarnings = @(
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
    'src/config/config_io.cpp|windows.h',
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
    'src/util/paths.cpp|windows.h',
    'src/util/scale.h|windows.h',
    'src/util/lightweight_mutex.cpp|windows.h',
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
    'src/util/text_encoding.cpp|windows.h',
    'src/telemetry/fps/fps_service_client_provider.cpp|windows.h'
)

$ignoredUnusedIncludeWarningSet = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($warning in $ignoredUnusedIncludeWarnings) {
    [void]$ignoredUnusedIncludeWarningSet.Add($warning)
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
    if ($ignoredUnusedIncludeWarningSet.Contains("$relativeSlashPath|$normalizedHeader")) {
        return $true
    }

    $headerLeaf = Get-HeaderKeyLeaf -Header $normalizedHeader
    return $ignoredUnusedIncludeWarningSet.Contains("$relativeSlashPath|$headerLeaf")
}

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

    Add-Candidate -List $List -Candidate (Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin\clangd.exe')
    Add-Candidate -List $List -Candidate (Join-Path $VsRoot 'VC\Tools\Llvm\bin\clangd.exe')
}

function Resolve-ClangdFromBuildDatabase {
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
        foreach ($candidate in @(
            Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin\clangd.exe'
            Join-Path $vsRoot 'VC\Tools\Llvm\bin\clangd.exe'
        )) {
            if (Test-Path -LiteralPath $candidate) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    return $null
}

function Resolve-ClangdPath {
    param(
        [string]$RepoRoot,
        [string]$CompileCommandsPath
    )

    $candidates = [System.Collections.Generic.List[string]]::new()
    Add-VsLlvmCandidates -List $candidates -VsRoot $env:VSINSTALLDIR

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command clangd.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $devenvCmd = Join-Path $RepoRoot 'devenv.cmd'
    if ($env:GITHUB_ACTIONS -ne 'true' -and (Test-Path -LiteralPath $devenvCmd)) {
        $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clangd.exe" 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $probe) {
            return $probe.Trim()
        }
    }

    $buildDatabaseCandidate = Resolve-ClangdFromBuildDatabase -DatabasePath $CompileCommandsPath
    if ($buildDatabaseCandidate) {
        return $buildDatabaseCandidate
    }

    foreach ($pattern in @(
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clangd.exe'
        'C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\bin\clangd.exe'
    )) {
        $discovered = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($discovered) {
            return $discovered.FullName
        }
    }

    return $null
}

function Test-EligibleIncludeLintFile {
    param(
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
    if ($fileName -eq 'board_gigabyte_siv.cpp' -or $fileName -eq 'board_gigabyte_siv_bridge.cpp' -or
        $fileName -eq 'board_msi_center_bridge.cpp') {
        return $false
    }
    return $true
}

function Get-TrackedIncludeLintFiles {
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
            Where-Object { Test-EligibleIncludeLintFile -FullPath $_.FullName }
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

function Get-ChangedIncludeLintFiles {
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
    } else {
        $gitDiffOutput = Invoke-GitOutput -RepoRoot $RepoRoot -Arguments @(
            'diff'
            '--name-only'
            '--diff-filter=ACMR'
            '--cached'
            '--'
            $pathSpecs
        )
    }

    foreach ($entry in $gitDiffOutput) {
        if (-not [string]::IsNullOrWhiteSpace($entry)) {
            $changedFiles.Add($entry.Trim())
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
        if (-not (Test-EligibleIncludeLintFile -FullPath $normalizedPath)) {
            continue
        }

        Get-Item -LiteralPath $normalizedPath
    }

    return $files | Sort-Object FullName
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

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$compileCommands = Join-Path $resolvedRoot 'build\cmake\compile_commands.json'
if (-not (Test-Path -LiteralPath $compileCommands)) {
    Write-Host 'build\cmake\compile_commands.json was not found. Build the project first with build.cmd.'
    exit 1
}

$files = if ($Scope -eq 'changed') {
    @(Get-ChangedIncludeLintFiles -RepoRoot $resolvedRoot)
} else {
    @(Get-TrackedIncludeLintFiles -RepoRoot $resolvedRoot)
}

if ($files.Count -eq 0) {
    if ($Scope -eq 'changed') {
        Write-Host 'No eligible changed project source or header files were found.'
        exit 0
    }

    Write-Host 'No eligible project source or header files were found.'
    exit 1
}

$includeFiles = [System.Collections.Generic.List[object]]::new()
$lineFiltersByPath = @{}
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

$parallelism = $MaxParallel
if ($parallelism -le 0) {
    $parallelism = [Math]::Min([Math]::Max([Environment]::ProcessorCount - 1, 1), 8)
}
$parallelism = [Math]::Min([Math]::Max($parallelism, 1), $files.Count)

$clangd = Resolve-ClangdPath -RepoRoot $resolvedRoot -CompileCommandsPath $compileCommands
if (-not $clangd) {
    Write-Host 'clangd.exe was not found. Install the Visual Studio LLVM tools or add clangd to PATH.'
    exit 1
}

$manifestPath = Join-Path $resolvedRoot 'build\clangd_include_files.json'
$manifest = foreach ($file in $files) {
    $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName
    [pscustomobject]@{
        relativePath = $relativePath
        lineFilter = $lineFiltersByPath[$relativePath]
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

$script = Join-Path $resolvedRoot 'tools\run_clangd_includes.py'
& python $script `
    --root $resolvedRoot `
    --clangd $clangd `
    --compile-commands-dir (Join-Path $resolvedRoot 'build\cmake') `
    --manifest $manifestPath `
    --report (Join-Path $resolvedRoot 'build\clang_include_cleaner_report.txt') `
    --scope $Scope `
    --parallelism $parallelism `
    --timeout-seconds $TimeoutSeconds
exit $LASTEXITCODE
