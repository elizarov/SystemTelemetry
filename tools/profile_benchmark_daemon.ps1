param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$daemonRoot = Join-Path $RepoRoot "build\profile_benchmark_daemon"
$requestsRoot = Join-Path $daemonRoot "requests"
$statusPath = Join-Path $daemonRoot "status.env"
$readyPath = Join-Path $daemonRoot "ready.flag"
$stopPath = Join-Path $daemonRoot "stop.flag"
$profileScript = Join-Path $RepoRoot "profile_benchmark.cmd"
$host.UI.RawUI.WindowTitle = "SystemTelemetry Benchmark Daemon"

function Test-AdminPrivilege {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-StatusFile {
    $pidText = "pid=$PID"
    Set-Content -LiteralPath $statusPath -Value $pidText -Encoding ascii
    Set-Content -LiteralPath $readyPath -Value "ready" -Encoding ascii
}

function Remove-DaemonFiles {
    Remove-Item -LiteralPath $readyPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $statusPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stopPath -Force -ErrorAction SilentlyContinue
}

function Read-RequestValue {
    param(
        [string]$RequestPath,
        [string]$Name,
        [string]$DefaultValue
    )

    if (-not (Test-Path -LiteralPath $RequestPath)) {
        return $DefaultValue
    }

    foreach ($line in Get-Content -LiteralPath $RequestPath) {
        if ($line -like "$Name=*") {
            return $line.Substring($Name.Length + 1)
        }
    }

    return $DefaultValue
}

if (-not (Test-AdminPrivilege)) {
    Write-Error "The benchmark daemon must run elevated."
}

Write-Host "[daemon] Starting benchmark daemon..."
Write-Host "[daemon] Repo root: $RepoRoot"
Write-Host "[daemon] Request root: $requestsRoot"

New-Item -ItemType Directory -Path $requestsRoot -Force | Out-Null
Remove-DaemonFiles
Write-StatusFile
Write-Host "[daemon] Ready. Waiting for benchmark requests."

try {
    while ($true) {
        if (Test-Path -LiteralPath $stopPath) {
            Write-Host "[daemon] Stop requested. Exiting."
            break
        }

        $requestDirs = Get-ChildItem -LiteralPath $requestsRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name
        foreach ($requestDir in $requestDirs) {
            $requestFile = Join-Path $requestDir.FullName "request.env"
            $doneFile = Join-Path $requestDir.FullName "done.env"
            $errorFile = Join-Path $requestDir.FullName "error.txt"
            $lockFile = Join-Path $requestDir.FullName "running.lock"

            if (-not (Test-Path -LiteralPath $requestFile)) {
                continue
            }
            if (Test-Path -LiteralPath $doneFile) {
                continue
            }
            if (Test-Path -LiteralPath $lockFile) {
                continue
            }

            Set-Content -LiteralPath $lockFile -Value "running" -Encoding ascii

            $iterations = Read-RequestValue -RequestPath $requestFile -Name "iterations" -DefaultValue "600"
            $renderScale = Read-RequestValue -RequestPath $requestFile -Name "render_scale" -DefaultValue "2"
            $etlPath = Join-Path $requestDir.FullName "layout_edit_benchmark_wpr.etl"
            $summaryPath = Join-Path $requestDir.FullName "layout_edit_benchmark_wpr.txt"
            $calltreePath = Join-Path $requestDir.FullName "layout_edit_benchmark_wpr_calltree.html"

            try {
                Write-Host "[daemon] Running request $($requestDir.Name) iterations=$iterations scale=$renderScale"
                $previousForceBuild = $env:PROFILE_BENCHMARK_FORCE_BUILD
                $env:PROFILE_BENCHMARK_FORCE_BUILD = "1"
                & $profileScript $iterations $renderScale /run-request $requestDir.FullName
                $exitCode = $LASTEXITCODE
                if (-not (Test-Path -LiteralPath $doneFile) -and -not (Test-Path -LiteralPath $errorFile)) {
                    $lines = @(
                        "exit_code=$exitCode"
                        "etl=$etlPath"
                        "summary=$summaryPath"
                        "calltree=$calltreePath"
                    )
                    Set-Content -LiteralPath $doneFile -Value $lines -Encoding ascii
                }
                if (Test-Path -LiteralPath $doneFile) {
                    Remove-Item -LiteralPath $errorFile -Force -ErrorAction SilentlyContinue
                }
                Write-Host "[daemon] Request $($requestDir.Name) finished with exit code $exitCode"
            } catch {
                Set-Content -LiteralPath $errorFile -Value $_.Exception.Message -Encoding utf8
                Write-Host "[daemon] Request $($requestDir.Name) failed: $($_.Exception.Message)"
            } finally {
                if ($null -eq $previousForceBuild) {
                    Remove-Item Env:PROFILE_BENCHMARK_FORCE_BUILD -ErrorAction SilentlyContinue
                } else {
                    $env:PROFILE_BENCHMARK_FORCE_BUILD = $previousForceBuild
                }
                Remove-Item -LiteralPath $lockFile -Force -ErrorAction SilentlyContinue
            }
        }

        Start-Sleep -Milliseconds 500
    }
} finally {
    Remove-DaemonFiles
    Write-Host "[daemon] Daemon stopped."
}
