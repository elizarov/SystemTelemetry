param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$iconRoot = Join-Path $buildRoot 'app_icon'
$exePath = Join-Path $buildRoot 'CaseDash.exe'
$icoPath = Join-Path $repoRoot 'resources\app.ico'
$sizes = @(16, 32, 64)

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        throw "build.cmd failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path $exePath)) {
    throw "CaseDash executable was not found at $exePath."
}

New-Item -ItemType Directory -Force -Path $iconRoot | Out-Null

$pngEntries = @()
foreach ($size in $sizes) {
    $pngPath = Join-Path $iconRoot ("app_icon_{0}.png" -f $size)
    $arguments = @('/default-config', '/theme:dark_cyan', "/app-icon:$pngPath", "/app-icon-size:$size", '/exit')
    $process = Start-Process -FilePath $exePath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "CaseDash app icon export failed for size $size with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path $pngPath)) {
        throw "CaseDash app icon export did not create $pngPath."
    }
    $pngEntries += [pscustomobject]@{ Size = $size; Path = $pngPath; Bytes = [IO.File]::ReadAllBytes($pngPath) }
}

$stream = [IO.File]::Create($icoPath)
try {
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$pngEntries.Count)

        $offset = 6 + ($pngEntries.Count * 16)
        foreach ($entry in $pngEntries) {
            $directorySize = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
            $writer.Write([Byte]$directorySize)
            $writer.Write([Byte]$directorySize)
            $writer.Write([Byte]0)
            $writer.Write([Byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$entry.Bytes.Length)
            $writer.Write([UInt32]$offset)
            $offset += $entry.Bytes.Length
        }

        foreach ($entry in $pngEntries) {
            $writer.Write($entry.Bytes)
        }
    } finally {
        $writer.Dispose()
    }
} finally {
    $stream.Dispose()
}

& python (Join-Path $repoRoot 'tools\optimize_png_resources.py') $icoPath
if ($LASTEXITCODE -ne 0) {
    throw "App icon PNG optimization failed with exit code $LASTEXITCODE."
}

Write-Host "Updated $icoPath from rendered dark_cyan app icon assets."
