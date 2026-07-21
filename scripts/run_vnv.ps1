# Verification & Validation runner (mod / int / bench).
# Always regenerates solutions from manifests, then builds and runs.
#
#   .\scripts\run_vnv.ps1              # mod only (default)
#   .\scripts\run_vnv.ps1 -All         # mod + int (full gate; bench stays opt-in)
#   .\scripts\run_vnv.ps1 -Int -UpdateGoldens
#   .\scripts\run_vnv.ps1 -Bench
#   .\scripts\run_vnv.ps1 -Bench -UpdateBaselines
#   .\scripts\run_vnv.ps1 -SkipSync -SkipBuild

param(
    [switch]$Mod,
    [switch]$Int,
    [switch]$Bench,
    [switch]$All,
    [switch]$UpdateGoldens,
    [switch]$UpdateBaselines,
    [switch]$SkipSync,
    [switch]$SkipBuild,
    [string]$Configuration = "Debug",
    [double]$BenchTol = 0.15
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

# Default = mod only unless -All / -Int / -Bench
$runMod = $true
$runInt = $false
$runBench = $false
if ($All) { $runMod = $true; $runInt = $true; $runBench = $false }
if ($Bench) { $runBench = $true }
if ($Int -and -not $Mod -and -not $All) { $runMod = $false; $runInt = $true }
elseif ($Mod -and -not $Int -and -not $All) { $runMod = $true; $runInt = $false }
elseif ($Int) { $runInt = $true }
if ($Bench -and -not $Mod -and -not $Int -and -not $All) { $runMod = $false }

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

function Compare-BenchJson {
    param(
        [string]$BaselinePath,
        [string]$ActualPath,
        [double]$Tol
    )
    if (-not (Test-Path -LiteralPath $BaselinePath)) {
        Write-Host "FAIL: no baseline at $BaselinePath (use -UpdateBaselines)"
        return $false
    }
    if (-not (Test-Path -LiteralPath $ActualPath)) {
        Write-Host "FAIL: missing actual $ActualPath"
        return $false
    }
    $base = Get-Content -LiteralPath $BaselinePath -Raw | ConvertFrom-Json
    $act = Get-Content -LiteralPath $ActualPath -Raw | ConvertFrom-Json

    $ok = $true
    function Check-Metric([string]$label, $baseMed, $actMed) {
        if ($null -eq $baseMed) { return }
        $limit = [double]$baseMed * (1.0 + $Tol)
        if ([double]$actMed -gt $limit) {
            Write-Host ("FAIL: {0} median {1:N3} > baseline {2:N3} * (1+{3}) = {4:N3}" -f `
                $label, [double]$actMed, [double]$baseMed, $Tol, $limit)
            $script:ok = $false
        } else {
            Write-Host ("  ok {0}: actual={1:N3} baseline={2:N3}" -f $label, [double]$actMed, [double]$baseMed)
        }
    }

    Check-Metric "frame_ms" $base.frame_ms.median $act.frame_ms.median
    Check-Metric "cpu_ms" $base.cpu_ms.median $act.cpu_ms.median
    Check-Metric "gpu_ms" $base.gpu_ms.median $act.gpu_ms.median

    # Tracked scopes if present on both sides
    $tracked = @("Prepare", "MainColorDepth", "MeshArenaUpload", "RecordMain", "Cubes", "DebugMesh")
    foreach ($name in $tracked) {
        $bScope = $base.scopes.$name
        $aScope = $act.scopes.$name
        if ($null -eq $bScope -or $null -eq $aScope) { continue }
        if ($null -ne $bScope.cpu_median) {
            Check-Metric "$name.cpu" $bScope.cpu_median $aScope.cpu_median
        }
        if ($null -ne $bScope.gpu_median -and [double]$bScope.gpu_median -gt 0) {
            Check-Metric "$name.gpu" $bScope.gpu_median $aScope.gpu_median
        }
    }
    return $ok
}

foreach ($p in $manifest.projects) {
    $cat = $p.category
    if ($cat -eq "mod" -and -not $runMod) { continue }
    if ($cat -eq "int" -and -not $runInt) { continue }
    if ($cat -eq "bench" -and -not $runBench) { continue }

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

    if ($p.kind -eq "perf_baseline") {
        $case = $p.case
        if (-not $case) { $case = "fake_steady_frame" }
        $outDir = Join-Path $RepoRoot "vnv\bench\tests\out\$case"
        $actual = Join-Path $outDir "actual.json"
        $baseline = Join-Path $RepoRoot "vnv\bench\baselines\$case.json"

        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        if (Test-Path $actual) { Remove-Item $actual -Force }

        $code = Run-Exe $exe @("--case", $case, "--out", $actual)
        if ($code -ne 0) {
            Write-Host "FAIL: bench harness exit $code"
            $failures++
            continue
        }

        if ($UpdateBaselines) {
            $bdir = Split-Path -Parent $baseline
            New-Item -ItemType Directory -Force -Path $bdir | Out-Null
            Copy-Item -Force $actual $baseline
            Write-Host "Updated baseline: $baseline"
            continue
        }

        if (-not (Compare-BenchJson -BaselinePath $baseline -ActualPath $actual -Tol $BenchTol)) {
            $failures++
        } else {
            Write-Host "PASS: bench $case"
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
