param(
    [Parameter(Mandatory = $true)]
    [string]$Root,

    [ValidateSet('check', 'fix')]
    [string]$Mode = 'check'
)

$ErrorActionPreference = 'Stop'

function Resolve-ClangTidyPath {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\bin\clang-tidy.exe"
        "$env:VSINSTALLDIR\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
        "$env:VSINSTALLDIR\VC\Tools\Llvm\bin\clang-tidy.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command clang-tidy.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $devenvCmd = Join-Path $Root 'devenv.cmd'
    if (Test-Path -LiteralPath $devenvCmd) {
        $probe = & cmd /c "call `"$devenvCmd`" >nul 2>&1 && where clang-tidy.exe" 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $probe) {
            return $probe.Trim()
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
                $_.Name -ne 'board_gigabyte_siv.cpp'
            }
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

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$compileCommands = Join-Path $resolvedRoot 'build\cmake\compile_commands.json'
$preferredReportPath = Join-Path $resolvedRoot 'build\clang_tidy_report.txt'
$clangTidy = Resolve-ClangTidyPath

if (-not $clangTidy) {
    Write-Host 'clang-tidy.exe was not found. Install the Visual Studio LLVM tools or add clang-tidy to PATH.'
    exit 1
}

if (-not (Test-Path -LiteralPath $compileCommands)) {
    Write-Host 'build\cmake\compile_commands.json was not found. Build the project first with build.cmd.'
    exit 1
}

$files = @(Get-TrackedCppFiles -RepoRoot $resolvedRoot)
if ($files.Count -eq 0) {
    Write-Host 'No eligible project translation units were found.'
    exit 1
}

$report = New-ReportWriter -PreferredPath $preferredReportPath
$reportPath = $report.Path
$writer = $report.Writer
try {
    $writer.WriteLine('clang-tidy report')
    $writer.WriteLine("Mode: $Mode")
    $writer.WriteLine("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')")
    $writer.WriteLine("Using clang-tidy: $clangTidy")
    $writer.WriteLine("Build database: $compileCommands")
    $writer.WriteLine()

    Write-Host "Using clang-tidy: $clangTidy"
    Write-Host "Running optional clang-tidy sweep for $($files.Count) translation units..."
    if ($report.UsedFallback) {
        Write-Host "Primary report path is locked; writing to $reportPath instead."
    }
    Write-Host "Writing full clang-tidy output to $reportPath"

    $tidyFailed = $false
    for ($index = 0; $index -lt $files.Count; $index++) {
        $file = $files[$index]
        $relativePath = Get-RelativeRepoPath -RepoRoot $resolvedRoot -FullPath $file.FullName

        Write-Host "[$($index + 1)/$($files.Count)] $relativePath"
        $writer.WriteLine('===============================================================================')
        $writer.WriteLine("[$($index + 1)/$($files.Count)] $relativePath")

        $commandArgs = @(
            '-p', (Join-Path $resolvedRoot 'build\cmake')
        )
        if ($Mode -eq 'fix') {
            $commandArgs += @('-fix', '--format-style=none')
        }
        $commandArgs += $relativePath

        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $output = & $clangTidy @commandArgs 2>&1
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference

        foreach ($entry in $output) {
            if ($null -eq $entry) {
                $writer.WriteLine()
            } else {
                $writer.WriteLine($entry.ToString())
            }
        }

        if ($exitCode -ne 0) {
            $tidyFailed = $true
            $writer.WriteLine("clang-tidy exit code: $exitCode")
        }
        $writer.WriteLine()
        $writer.Flush()
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
