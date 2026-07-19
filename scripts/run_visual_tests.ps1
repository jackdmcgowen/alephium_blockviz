# Compatibility wrapper → VnV int visual suite.
# Prefer: .\scripts\run_vnv.ps1 -Int  /  -All -UpdateGoldens

param(
    [string]$Case = "fake_overview",
    [string]$Configuration = "Debug",
    [switch]$UpdateGoldens,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$args = @("-Int", "-Configuration", $Configuration)
if ($UpdateGoldens) { $args += "-UpdateGoldens" }
if ($SkipBuild) { $args += "-SkipBuild" }
# Case is fixed in manifest for V1; Case param kept for CLI compatibility.
& (Join-Path $PSScriptRoot "run_vnv.ps1") @args
exit $LASTEXITCODE
