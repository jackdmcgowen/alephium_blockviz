# Verification & Validation runner (mod / int).
# Always regenerates solutions from manifests, then builds and runs.
#
#   .\scripts\run_vnv.ps1              # mod only (default)
#   .\scripts\run_vnv.ps1 -All         # mod + int (full gate)
#   .\scripts\run_vnv.ps1 -Int -UpdateGoldens
#   .\scripts\run_vnv.ps1 -SkipSync -SkipBuild

param(
    [switch]$Mod,
    [switch]$Int,
    [switch]$All,
    [switch]$UpdateGoldens,
    [switch]$SkipSync,
    [switch]$SkipBuild,
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

# Default = mod only unless -All or -Int
$runMod = $true
$runInt = $false
if ($All) { $runMod = $true; $runInt = $true }
elseif ($Int -and -not $Mod) { $runMod = $false; $runInt = $true }
elseif ($Mod -and -not $Int) { $runMod = $true; $runInt = $false }
elseif ($Int) { $runInt = $true }

$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msb) {
    $cand = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path $cand) { $msb = $cand }
}
if (-not $msb -or -not (Test-Path $msb)) { throw "MSBuild not found" }

if (-not $SkipSync) {
    Write-Host "== sync_solutions =="
    & (Join-Path $PSScriptRoot "sync_solutions.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$vnvSln = Join-Path $RepoRoot "sln\blockviz_vnv.sln"
if (-not $SkipBuild) {
    Write-Host "== MSBuild blockviz_vnv.sln ($Configuration|x64) =="
    & $msb $vnvSln /p:Configuration=$Configuration /p:Platform=x64 /v:m /nologo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$manifest = Get-Content (Join-Path $RepoRoot "vnv\manifest\vnv_projects.json") -Raw | ConvertFrom-Json
$failures = 0

function Run-Exe([string]$path, [string[]]$exeArgs) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "FAIL: missing exe $path"
        return 1
    }
    # Start-Process avoids PowerShell capturing native stdout as function output
    # (which would pollute $code when callers assign Run-Exe's return value).
    $psi = @{
        FilePath         = $path
        WorkingDirectory = $RepoRoot
        Wait             = $true
        PassThru         = $true
        NoNewWindow      = $true
    }
    if ($exeArgs -and $exeArgs.Count -gt 0) {
        $psi.ArgumentList = $exeArgs
    }
    $p = Start-Process @psi
    return [int]$p.ExitCode
}

foreach ($p in $manifest.projects) {
    $cat = $p.category
    if ($cat -eq "mod" -and -not $runMod) { continue }
    if ($cat -eq "int" -and -not $runInt) { continue }
    if ($cat -eq "bench") { continue }

    $key = $p.out_dir_key
    if (-not $key) { $key = $p.id }
    $exe = Join-Path $RepoRoot "build\$key\$Configuration\$($p.exe_name)"

    Write-Host ""
    Write-Host "== VnV [$cat] $($p.id) =="

    if ($p.kind -eq "visual_golden") {
        $case = $p.case
        if (-not $case) { $case = "fake_overview" }
        $outDir = Join-Path $RepoRoot "vnv\int\tests\visual\out\$case"
        $actual = Join-Path $outDir "actual.png"
        $golden = Join-Path $RepoRoot "vnv\int\tests\visual\goldens\$case.png"
        $diff = Join-Path $outDir "diff.png"
        $report = Join-Path $outDir "report.txt"
        $compare = Join-Path $RepoRoot "vnv\int\tests\visual\compare_images.ps1"

        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        if (Test-Path $actual) { Remove-Item $actual -Force }

        $code = Run-Exe $exe @("--case", $case, "--out", $actual)
        if ($code -ne 0) {
            Write-Host "FAIL: harness exit $code"
            $failures++
            continue
        }

        if ($UpdateGoldens) {
            $gdir = Split-Path -Parent $golden
            New-Item -ItemType Directory -Force -Path $gdir | Out-Null
            Copy-Item -Force $actual $golden
            Write-Host "Updated golden: $golden"
            continue
        }

        if (-not (Test-Path $golden)) {
            Write-Host "FAIL: no golden at $golden (use -UpdateGoldens)"
            $failures++
            continue
        }

        & $compare -Expected $golden -Actual $actual -DiffOut $diff -ReportOut $report `
            -PerPixelTol 8 -MaxBadFraction 0.002
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: visual compare"
            $failures++
        } else {
            Write-Host "PASS: visual $case"
        }
        continue
    }

    # Default: plain exe
    $code = Run-Exe $exe @()
    if ($code -ne 0) {
        Write-Host "FAIL: $($p.id) exit $code"
        $failures++
    } else {
        Write-Host "PASS: $($p.id)"
    }
}

Write-Host ""
if ($failures -gt 0) {
    Write-Host "VnV finished with $failures failure(s)"
    exit 1
}
Write-Host "VnV finished: all selected suites passed"
exit 0
