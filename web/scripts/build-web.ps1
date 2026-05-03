param(
    [switch]$Clean,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraArgs
)

$ErrorActionPreference = 'Stop'

foreach ($arg in $ExtraArgs) {
    if ($arg -ieq 'clean' -or $arg -ieq '-clean' -or $arg -ieq '/clean') {
        $Clean = $true
    } else {
        throw "Unknown web build option '$arg'. Use 'clean' to force regenerated assets."
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$webDir = Split-Path -Parent $scriptDir
$repoRoot = Split-Path -Parent $webDir
$distDir = Join-Path $webDir 'dist'
$assetDir = Join-Path $distDir 'assets'
$generatedDir = Join-Path $assetDir 'generated'
$configPath = Join-Path $repoRoot 'resources\config.ini'
$versionPath = Join-Path $repoRoot 'VERSION'
$exePath = Join-Path $repoRoot 'build\CaseDash.exe'

function Remove-DirectoryIfPresent {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Copy-Directory {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Read-ThemeConfig {
    param([string]$Path)

    $themes = @()
    $currentTheme = $null
    $defaultTheme = $null
    $inDisplay = $false

    foreach ($rawLine in Get-Content -LiteralPath $Path) {
        $line = $rawLine.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith(';') -or $line.StartsWith('#')) {
            continue
        }

        if ($line -match '^\[(.+)\]$') {
            $section = $Matches[1]
            $inDisplay = ($section -eq 'display')
            if ($section -match '^theme\.(.+)$') {
                $currentTheme = [ordered]@{
                    id = $Matches[1]
                    description = ''
                    colors = [ordered]@{}
                }
                $themes += $currentTheme
            } else {
                $currentTheme = $null
            }
            continue
        }

        if ($line -notmatch '^([^=]+?)\s*=\s*(.*)$') {
            continue
        }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()

        if ($inDisplay -and $key -eq 'theme') {
            $defaultTheme = $value
        }

        if ($null -ne $currentTheme) {
            if ($key -eq 'description') {
                $currentTheme['description'] = $value
            } elseif ($key -in @('background', 'foreground', 'accent', 'guide')) {
                $currentTheme['colors'][$key] = $value
            }
        }
    }

    foreach ($theme in $themes) {
        foreach ($token in @('background', 'foreground', 'accent', 'guide')) {
            if (-not $theme['colors'].Contains($token)) {
                throw "Theme '$($theme['id'])' is missing token '$token'."
            }
        }
    }

    if (-not $defaultTheme) {
        throw "Default display theme was not found in $Path."
    }

    return @{
        defaultTheme = $defaultTheme
        themes = $themes
    }
}

function Wait-GeneratedFile {
    param([string]$Path)

    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($null -ne $item -and $item.Length -gt 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "Expected generated file was not created: $Path"
}

function Test-GeneratedFile {
    param([string]$Path)

    $item = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    return ($null -ne $item -and $item.Length -gt 0)
}

function Invoke-CaseDashThemeAssetExport {
    param(
        [string]$Theme,
        [string]$DashboardPath,
        [string]$GuidePath,
        [string]$IconPath,
        [int]$ThemeIndex,
        [int]$ThemeCount
    )

    $outputPaths = @($DashboardPath, $GuidePath, $IconPath)
    $hasAllOutputs = $true
    foreach ($path in $outputPaths) {
        if (-not (Test-GeneratedFile $path)) {
            $hasAllOutputs = $false
            break
        }
    }

    $progressLabel = "[$ThemeIndex/$ThemeCount] $Theme"
    if (-not $Clean -and $hasAllOutputs) {
        Write-Host "Reusing generated assets for $progressLabel"
        return
    }

    Write-Host "Generating theme assets for $progressLabel"
    $arguments = @('/fake',
        '/default-config',
        "/theme:$Theme",
        '/scale:2',
        "/screenshot:$DashboardPath",
        "/layout-guide-sheet:$GuidePath",
        "/app-icon:$IconPath",
        '/app-icon-size:128',
        '/exit')
    $process = Start-Process -FilePath $exePath -ArgumentList $arguments -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) {
        throw "CaseDash website asset export failed for theme '$Theme' with exit code $($process.ExitCode)."
    }
    foreach ($path in $outputPaths) {
        Wait-GeneratedFile $path
    }
    Write-Host "Generated theme assets for $progressLabel"
}

& (Join-Path $repoRoot 'build.cmd')
if ($LASTEXITCODE -ne 0) {
    throw "build.cmd failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "CaseDash.exe was not found at $exePath."
}

$themeConfig = Read-ThemeConfig $configPath
$versionText = (Get-Content -LiteralPath $versionPath -TotalCount 1).Trim()
if (-not $versionText) {
    throw "VERSION is empty."
}

if ($Clean) {
    Remove-DirectoryIfPresent $distDir
}
New-Item -ItemType Directory -Force -Path $generatedDir | Out-Null

$indexHtml = (Get-Content -LiteralPath (Join-Path $webDir 'index.html') -Raw).Replace('{{VERSION}}', $versionText)
Set-Content -LiteralPath (Join-Path $distDir 'index.html') -Value $indexHtml -Encoding UTF8
Remove-DirectoryIfPresent (Join-Path $distDir 'src')
Copy-Directory -Source (Join-Path $webDir 'src') -Destination (Join-Path $distDir 'src')

$siteThemes = @()
$themeIndex = 0
$themeCount = $themeConfig.themes.Count
foreach ($theme in $themeConfig.themes) {
    $themeIndex += 1
    $themeId = $theme['id']
    $dashboardName = "$themeId-dashboard.png"
    $guideName = "$themeId-guide.png"
    $iconName = "$themeId-icon.png"
    $dashboardPath = Join-Path $generatedDir $dashboardName
    $guidePath = Join-Path $generatedDir $guideName
    $iconPath = Join-Path $generatedDir $iconName

    Invoke-CaseDashThemeAssetExport `
        -Theme $themeId `
        -DashboardPath $dashboardPath `
        -GuidePath $guidePath `
        -IconPath $iconPath `
        -ThemeIndex $themeIndex `
        -ThemeCount $themeCount

    $siteThemes += [ordered]@{
        id = $themeId
        name = ($themeId -replace '_', ' ')
        description = $theme['description']
        colors = $theme['colors']
        assets = [ordered]@{
            dashboard = "assets/generated/$dashboardName"
            guide = "assets/generated/$guideName"
            icon = "assets/generated/$iconName"
        }
    }
}

$siteData = [ordered]@{
    defaultTheme = $themeConfig.defaultTheme
    themes = $siteThemes
}

$json = $siteData | ConvertTo-Json -Depth 8
$siteDataJs = "window.CaseDashSite = $json;`r`n"
Set-Content -LiteralPath (Join-Path $assetDir 'site-data.js') -Value $siteDataJs -Encoding UTF8

Write-Host "Website built: $(Join-Path $distDir 'index.html')"
