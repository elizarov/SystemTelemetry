param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$exePath = Join-Path $repoRoot 'build\CaseDash.exe'
$imageDir = Join-Path $repoRoot 'docs\image'

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        throw "build.cmd failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "CaseDash.exe was not found at $exePath. Run build.cmd first or omit -SkipBuild."
}

New-Item -ItemType Directory -Force -Path $imageDir | Out-Null

$images = @(
    @{ Theme = 'dark_cyan'; FileName = 'casedash-screenshot-dark.png' },
    @{ Theme = 'cobalt_white'; FileName = 'casedash-screenshot-light.png' }
)

function Wait-GeneratedFile {
    param(
        [string]$Path
    )

    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($null -ne $item -and $item.Length -gt 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "CaseDash screenshot export did not create $Path."
}

foreach ($image in $images) {
    $outputPath = Join-Path $imageDir $image.FileName
    $arguments = @('/fake', '/default-config', "/theme:$($image.Theme)", '/scale:2', "/screenshot:$outputPath", '/exit')
    $process = Start-Process -FilePath $exePath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "CaseDash screenshot export failed for theme '$($image.Theme)' with exit code $($process.ExitCode)."
    }
    Wait-GeneratedFile $outputPath
    Write-Host "Generated $outputPath"
}
