param(
    [Parameter(Mandatory = $true)]
    [string]$CallTreePath,

    [string]$ProcessName = "SystemTelemetryBenchmarks.exe",

    [int]$Top = 12
)

$ErrorActionPreference = "Stop"

function Convert-CellText {
    param([string]$Html)

    $text = [regex]::Replace($Html, "<[^>]+>", "")
    $text = [System.Net.WebUtility]::HtmlDecode($text)
    return ($text -replace "\s+", " ").Trim()
}

function Read-TableRows {
    param(
        [string]$Html,
        [string]$TableId
    )

    $tableMatch = [regex]::Match(
        $Html,
        "<a\s+id=['""]$TableId['""][\s\S]*?<tbody>(?<body>[\s\S]*?)</tbody>",
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $tableMatch.Success) {
        return @()
    }

    $rows = @()
    foreach ($rowMatch in [regex]::Matches(
            $tableMatch.Groups["body"].Value,
            "<tr[^>]*>(?<row>[\s\S]*?)</tr>",
            [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
        $cells = @()
        foreach ($cellMatch in [regex]::Matches(
                $rowMatch.Groups["row"].Value,
                "<td[^>]*>(?<cell>[\s\S]*?)</td>",
                [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            $cells += Convert-CellText $cellMatch.Groups["cell"].Value
        }
        if ($cells.Count -gt 0) {
            $rows += , $cells
        }
    }
    return $rows
}

function Write-Rows {
    param(
        [string]$Title,
        [array]$Rows
    )

    Write-Output $Title
    if ($Rows.Count -eq 0) {
        Write-Output "  (no rows exported)"
        return
    }

    foreach ($row in $Rows) {
        Write-Output ("  {0,7} {1,7}  {2}" -f $row.Hits, $row.Percent, $row.Name)
    }
}

if (-not (Test-Path -LiteralPath $CallTreePath)) {
    throw "Call tree file not found: $CallTreePath"
}

$html = Get-Content -LiteralPath $CallTreePath -Raw

$moduleExclusiveRows = Read-TableRows -Html $html -TableId "TblME" |
    Select-Object -First $Top |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_[0]
            Hits = $_[1]
            Percent = $_[2]
        }
    }

$moduleInclusiveRows = Read-TableRows -Html $html -TableId "TblMI" |
    Select-Object -First $Top |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_[0]
            Hits = $_[1]
            Percent = $_[2]
        }
    }

$functionRows = Read-TableRows -Html $html -TableId "TblSI" |
    Where-Object { $_.Count -ge 4 -and $_[0] -like "$ProcessName!*" -and $_[0] -notmatch "!(__scrt_|invoke_main|main$)" } |
    Select-Object -First $Top |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_[0]
            Hits = $_[1]
            Percent = $_[2]
        }
    }

$exclusiveFunctionRows = Read-TableRows -Html $html -TableId "TblSE" |
    Where-Object { $_.Count -ge 4 -and $_[0] -like "$ProcessName!*" -and $_[0] -notmatch "!(__scrt_|invoke_main|main$)" } |
    Select-Object -First $Top |
    ForEach-Object {
        [pscustomobject]@{
            Name = $_[0]
            Hits = $_[1]
            Percent = $_[2]
        }
    }

Write-Output "Hotspot summary for $ProcessName"
Write-Output "Source: $CallTreePath"
Write-Output ""
Write-Rows "Top modules by exclusive hits:" $moduleExclusiveRows
Write-Output ""
Write-Rows "Top modules by inclusive hits:" $moduleInclusiveRows
Write-Output ""
Write-Rows "Top app functions by inclusive hits:" $functionRows
Write-Output ""
Write-Rows "Top app functions by exclusive hits:" $exclusiveFunctionRows
