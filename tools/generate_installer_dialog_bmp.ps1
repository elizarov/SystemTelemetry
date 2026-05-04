param(
    [switch]$SkipBuild,
    [string]$BmpPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$assetRoot = Join-Path $buildRoot 'installer_dialog_bmp'
$exePath = Join-Path $buildRoot 'CaseDash.exe'
$iconPath = Join-Path $assetRoot 'dark_cyan_app_icon_144.png'
if ([string]::IsNullOrWhiteSpace($BmpPath)) {
    $BmpPath = Join-Path $assetRoot 'CaseDash_WixUIDialogBmp.bmp'
}

Add-Type -AssemblyName System.Drawing

$dialogWidth = 493
$dialogHeight = 312
$leftColumnWidth = 132
$iconSize = 96
$iconTop = 42
$textTop = 152
$textMaxWidth = $leftColumnWidth - 12
$wixDialogBackground = [Drawing.Color]::FromArgb(255, 255, 255)

if (-not $SkipBuild) {
    & (Join-Path $repoRoot 'build.cmd')
    if ($LASTEXITCODE -ne 0) {
        throw "build.cmd failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path $exePath)) {
    throw "CaseDash executable was not found at $exePath."
}

New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null

$arguments = @('/default-config', '/theme:dark_cyan', "/app-icon:$iconPath", '/app-icon-size:144', '/exit')
$process = Start-Process -FilePath $exePath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "CaseDash app icon export failed with exit code $($process.ExitCode)."
}
if (-not (Test-Path $iconPath)) {
    throw "CaseDash app icon export did not create $iconPath."
}

$bitmap = [Drawing.Bitmap]::new($dialogWidth, $dialogHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
try {
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear($wixDialogBackground)
        $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::ClearTypeGridFit

        $icon = [Drawing.Image]::FromFile($iconPath)
        try {
            $iconLeft = [Math]::Floor(($leftColumnWidth - $iconSize) / 2)
            $graphics.DrawImage($icon, [Drawing.Rectangle]::new($iconLeft, $iconTop, $iconSize, $iconSize))
        } finally {
            $icon.Dispose()
        }

        $fontSize = 24.0
        $fontStyle = [Drawing.FontStyle]::Bold
        do {
            $font = [Drawing.Font]::new('Segoe UI', $fontSize, $fontStyle, [Drawing.GraphicsUnit]::Pixel)
            $textSize = $graphics.MeasureString('CaseDash', $font)
            if ($textSize.Width -le $textMaxWidth -or $fontSize -le 18.0) {
                break
            }
            $font.Dispose()
            $fontSize -= 0.5
        } while ($true)

        try {
            $textLeft = [Math]::Max(0, [Math]::Floor(($leftColumnWidth - $textSize.Width) / 2))
            $textRect = [Drawing.RectangleF]::new($textLeft, $textTop, $textMaxWidth, 42)
            $format = [Drawing.StringFormat]::new()
            try {
                $format.Alignment = [Drawing.StringAlignment]::Near
                $format.LineAlignment = [Drawing.StringAlignment]::Near
                $format.FormatFlags = [Drawing.StringFormatFlags]::NoWrap
                $format.Trimming = [Drawing.StringTrimming]::None
                $brush = [Drawing.SolidBrush]::new([Drawing.Color]::Black)
                try {
                    $graphics.DrawString('CaseDash', $font, $brush, $textRect, $format)
                } finally {
                    $brush.Dispose()
                }
            } finally {
                $format.Dispose()
            }
        } finally {
            $font.Dispose()
        }
    } finally {
        $graphics.Dispose()
    }

    $bmpDirectory = Split-Path -Parent $BmpPath
    if (-not [string]::IsNullOrWhiteSpace($bmpDirectory)) {
        New-Item -ItemType Directory -Force -Path $bmpDirectory | Out-Null
    }
    $bitmap.Save($BmpPath, [Drawing.Imaging.ImageFormat]::Bmp)
} finally {
    $bitmap.Dispose()
}

Write-Host "Updated $BmpPath from rendered dark_cyan app icon assets."
