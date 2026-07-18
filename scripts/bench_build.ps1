# Benchmark clean / incremental Debug|x64 builds.
# Usage: powershell -File scripts/bench_build.ps1 [-Label "baseline"] [-SkipClean]
param(
    [string]$Label = "bench",
    [switch]$SkipClean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found" }

$sln = Join-Path $root "sln\alephium_visualizer.sln"
$benchDir = Join-Path $root "build\bench"
New-Item -ItemType Directory -Force -Path $benchDir | Out-Null
$csv = Join-Path $benchDir "build_times.csv"
if (-not (Test-Path $csv)) {
    "timestamp,label,mode,seconds" | Set-Content $csv -Encoding utf8
}

function Invoke-TimedBuild([string]$mode, [string]$target) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $msbuild $sln /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /t:$target
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $target" }
    $sw.Stop()
    $sec = [math]::Round($sw.Elapsed.TotalSeconds, 2)
    $row = "{0},{1},{2},{3}" -f (Get-Date -Format o), $Label, $mode, $sec
    Add-Content $csv $row
    Write-Host ("{0,-16} {1,8:n2}s" -f $mode, $sec)
    return $sec
}

Write-Host "=== Build bench label=$Label ==="
Write-Host "MSBuild: $msbuild"

if (-not $SkipClean) {
    Invoke-TimedBuild "clean_rebuild" "Rebuild"
} else {
    Invoke-TimedBuild "build" "Build"
}

# Incremental: touch three hot files
$touchFiles = @(
    "src\graphics\graphics_system.cpp",
    "src\network\alephium\alephium_adapter.cpp",
    "src\app\main.cpp"
)
foreach ($rel in $touchFiles) {
    $path = Join-Path $root $rel
    if (-not (Test-Path $path)) { continue }
    (Get-Item $path).LastWriteTime = Get-Date
    $mode = "incr_" + [IO.Path]::GetFileNameWithoutExtension($rel)
    Invoke-TimedBuild $mode "Build"
}

Write-Host "Logged to $csv"
