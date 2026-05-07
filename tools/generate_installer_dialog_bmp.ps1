param(
    [switch]$SkipBuild,
    [string]$BmpPath,
    [string]$BannerBmpPath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$assetRoot = Join-Path $buildRoot 'installer_dialog_bmp'
$exePath = Join-Path $buildRoot 'CaseDash.exe'
$dialogIconPath = Join-Path $assetRoot 'dark_cyan_app_icon_96.png'
$bannerIconPath = Join-Path $assetRoot 'dark_cyan_app_icon_40.png'
if ([string]::IsNullOrWhiteSpace($BmpPath)) {
    $BmpPath = Join-Path $assetRoot 'CaseDash_WixUIDialogBmp.bmp'
}
if ([string]::IsNullOrWhiteSpace($BannerBmpPath)) {
    $BannerBmpPath = Join-Path $assetRoot 'CaseDash_WixUIBannerBmp.bmp'
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
$dialogIconSize = $iconSize

$bannerWidth = 493
$bannerHeight = 58
$bannerRightPanelLeft = 397
$bannerUnderlineHeight = 3
$bannerIconSize = 40
$wixBannerBackground = [Drawing.Color]::FromArgb(245, 248, 250)
$wixAccent = [Drawing.Color]::FromArgb(0, 184, 230)

function Set-HighQualityGraphics {
    param($Graphics)

    $Graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
    $Graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $Graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $Graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $Graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::ClearTypeGridFit
}

function Save-Bmp {
    param($Bitmap, [string]$OutputPath)

    $bmpDirectory = Split-Path -Parent $OutputPath
    if (-not [string]::IsNullOrWhiteSpace($bmpDirectory)) {
        New-Item -ItemType Directory -Force -Path $bmpDirectory | Out-Null
    }
    $Bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Bmp)
}

function Export-AppIcon {
    param([string]$OutputPath, [int]$Size)

    $arguments = @('/default-config', '/theme:dark_cyan', "/app-icon:$OutputPath", "/app-icon-size:$Size", '/exit')
    $process = Start-Process -FilePath $exePath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "CaseDash app icon export failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path $OutputPath)) {
        throw "CaseDash app icon export did not create $OutputPath."
    }
}

function Render-WixDialogBmp {
    param([string]$IconPath, [string]$OutputPath)

    $bitmap = [Drawing.Bitmap]::new($dialogWidth, $dialogHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear($wixDialogBackground)
            Set-HighQualityGraphics -Graphics $graphics

            $icon = [Drawing.Image]::FromFile($IconPath)
            try {
                if ($icon.Width -ne $iconSize -or $icon.Height -ne $iconSize) {
                    throw "Dialog app icon at $IconPath must be ${iconSize}x${iconSize} pixels."
                }
                $iconLeft = [Math]::Floor(($leftColumnWidth - $iconSize) / 2)
                $graphics.DrawImageUnscaled($icon, [int]$iconLeft, [int]$iconTop)
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

        Save-Bmp -Bitmap $bitmap -OutputPath $OutputPath
    } finally {
        $bitmap.Dispose()
    }
}

function Render-WixBannerBmp {
    param([string]$IconPath, [string]$OutputPath)

    $bitmap = [Drawing.Bitmap]::new($bannerWidth, $bannerHeight, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear($wixBannerBackground)
            Set-HighQualityGraphics -Graphics $graphics

            $icon = [Drawing.Image]::FromFile($IconPath)
            try {
                if ($icon.Width -ne $bannerIconSize -or $icon.Height -ne $bannerIconSize) {
                    throw "Banner app icon at $IconPath must be ${bannerIconSize}x${bannerIconSize} pixels."
                }
                $iconLeft = $bannerRightPanelLeft + [Math]::Floor(($bannerWidth - $bannerRightPanelLeft - $bannerIconSize) / 2)
                $iconTop = [Math]::Floor(($bannerHeight - $bannerUnderlineHeight - $bannerIconSize) / 2)
                $graphics.DrawImageUnscaled($icon, [int]$iconLeft, [int]$iconTop)
            } finally {
                $icon.Dispose()
            }

            $accentBrush = [Drawing.SolidBrush]::new($wixAccent)
            try {
                $underlineRect = [Drawing.Rectangle]::new(
                    0,
                    $bannerHeight - $bannerUnderlineHeight,
                    $bannerWidth,
                    $bannerUnderlineHeight)
                $graphics.FillRectangle($accentBrush, $underlineRect)
            } finally {
                $accentBrush.Dispose()
            }
        } finally {
            $graphics.Dispose()
        }

        Save-Bmp -Bitmap $bitmap -OutputPath $OutputPath
    } finally {
        $bitmap.Dispose()
    }
}

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

Export-AppIcon -OutputPath $dialogIconPath -Size $dialogIconSize
Export-AppIcon -OutputPath $bannerIconPath -Size $bannerIconSize
Render-WixDialogBmp -IconPath $dialogIconPath -OutputPath $BmpPath
Render-WixBannerBmp -IconPath $bannerIconPath -OutputPath $BannerBmpPath

Write-Host "Updated $BmpPath and $BannerBmpPath from rendered dark_cyan app icon assets."
