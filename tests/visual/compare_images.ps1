# Pixel-diff two PNGs with per-channel tolerance (Windows / System.Drawing).
# Exit 0 = pass, 1 = fail, 2 = usage/IO error.
param(
    [Parameter(Mandatory = $true)][string]$Expected,
    [Parameter(Mandatory = $true)][string]$Actual,
    [string]$DiffOut = "",
    [int]$PerPixelTol = 8,
    [double]$MaxBadFraction = 0.002,
    [string]$ReportOut = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Fail([string]$msg) {
    Write-Host "FAIL: $msg"
    exit 1
}

if (-not (Test-Path -LiteralPath $Expected)) { Write-Host "ERROR: missing expected: $Expected"; exit 2 }
if (-not (Test-Path -LiteralPath $Actual)) { Write-Host "ERROR: missing actual: $Actual"; exit 2 }

$exp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Expected))
$act = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Actual))
try {
    if ($exp.Width -ne $act.Width -or $exp.Height -ne $act.Height) {
        Fail "size mismatch expected=$($exp.Width)x$($exp.Height) actual=$($act.Width)x$($act.Height)"
    }

    $w = $exp.Width
    $h = $exp.Height
    $total = [int64]$w * [int64]$h
    $bad = [int64]0
    $maxDelta = 0
    $diffBmp = $null
    if ($DiffOut) {
        $diffBmp = New-Object System.Drawing.Bitmap $w, $h
    }

    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c0 = $exp.GetPixel($x, $y)
            $c1 = $act.GetPixel($x, $y)
            $dr = [Math]::Abs([int]$c0.R - [int]$c1.R)
            $dg = [Math]::Abs([int]$c0.G - [int]$c1.G)
            $db = [Math]::Abs([int]$c0.B - [int]$c1.B)
            $d = [Math]::Max($dr, [Math]::Max($dg, $db))
            if ($d -gt $maxDelta) { $maxDelta = $d }
            $isBad = $d -gt $PerPixelTol
            if ($isBad) { $bad++ }
            if ($diffBmp) {
                # Heat: scale absolute max channel delta to red
                $v = [Math]::Min(255, $d * 4)
                if ($isBad) {
                    $diffBmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, $v, 0, 0))
                } else {
                    $diffBmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(255, 0, $v, 0))
                }
            }
        }
    }

    $frac = if ($total -gt 0) { $bad / $total } else { 1.0 }
    $lines = @(
        "expected=$Expected"
        "actual=$Actual"
        "size=${w}x${h}"
        "per_pixel_tol=$PerPixelTol"
        "max_bad_fraction=$MaxBadFraction"
        "bad_pixels=$bad"
        "bad_fraction=$([string]::Format('{0:F6}', $frac))"
        "max_channel_delta=$maxDelta"
    )
    $report = ($lines -join "`n")
    Write-Host $report
    if ($ReportOut) {
        $dir = Split-Path -Parent $ReportOut
        if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        Set-Content -Path $ReportOut -Value $report -Encoding utf8
    }
    if ($diffBmp -and $DiffOut) {
        $dir = Split-Path -Parent $DiffOut
        if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
        $diffBmp.Save($DiffOut, [System.Drawing.Imaging.ImageFormat]::Png)
    }

    if ($frac -gt $MaxBadFraction) {
        Fail "bad_fraction $frac > $MaxBadFraction (bad_pixels=$bad max_delta=$maxDelta)"
    }
    Write-Host "PASS"
    exit 0
}
finally {
    $exp.Dispose()
    $act.Dispose()
    if ($diffBmp) { $diffBmp.Dispose() }
}
