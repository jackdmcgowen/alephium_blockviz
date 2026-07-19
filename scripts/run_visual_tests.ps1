# Build + run graphics visual regression harness, then pixel-compare to goldens.
# Run from anywhere; resolves repo root from this script location.
#
#   .\scripts\run_visual_tests.ps1
#   .\scripts\run_visual_tests.ps1 -UpdateGoldens
#   .\scripts\run_visual_tests.ps1 -Case fake_overview -Configuration Release

param(
    [string]$Case = "fake_overview",
    [string]$Configuration = "Debug",
    [switch]$UpdateGoldens,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msb) {
    $msb = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
}
if (-not (Test-Path $msb)) { throw "MSBuild not found" }

$proj = Join-Path $RepoRoot "sln\graphics_visual_tests.vcxproj"
$exe = Join-Path $RepoRoot "build\graphics_visual_tests\$Configuration\graphics_visual_tests.exe"
$outDir = Join-Path $RepoRoot "tests\visual\out\$Case"
$actual = Join-Path $outDir "actual.png"
$golden = Join-Path $RepoRoot "tests\visual\goldens\$Case.png"
$diff = Join-Path $outDir "diff.png"
$report = Join-Path $outDir "report.txt"
$compare = Join-Path $RepoRoot "tests\visual\compare_images.ps1"

if (-not $SkipBuild) {
    Write-Host "== Build graphics_visual_tests ($Configuration|x64) =="
    & $msb $proj /p:Configuration=$Configuration /p:Platform=x64 /v:m /nologo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
if (Test-Path $actual) { Remove-Item $actual -Force }

Write-Host "== Capture $Case =="
& $exe --case $Case --out $actual
if ($LASTEXITCODE -ne 0) {
    Write-Host "Harness failed with exit $LASTEXITCODE"
    exit $LASTEXITCODE
}
if (-not (Test-Path $actual)) {
    Write-Host "Missing actual PNG: $actual"
    exit 1
}

if ($UpdateGoldens) {
    $gdir = Split-Path -Parent $golden
    New-Item -ItemType Directory -Force -Path $gdir | Out-Null
    Copy-Item -Force $actual $golden
    Write-Host "Updated golden: $golden"
    exit 0
}

if (-not (Test-Path $golden)) {
    Write-Host "No golden at $golden"
    Write-Host "Create one with: .\scripts\run_visual_tests.ps1 -UpdateGoldens -Case $Case"
    exit 2
}

Write-Host "== Compare =="
& $compare -Expected $golden -Actual $actual -DiffOut $diff -ReportOut $report `
    -PerPixelTol 8 -MaxBadFraction 0.002
exit $LASTEXITCODE
