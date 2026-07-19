# Regenerate product + VnV Visual Studio solutions from vnv/manifest/*.json.
# Run after adding/removing/renaming shared library or VnV projects.
#
#   .\scripts\sync_solutions.ps1
#   .\scripts\sync_solutions.ps1 -CheckOnly
#   .\scripts\sync_solutions.ps1 -Verbose

param(
    [switch]$CheckOnly,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

$CppProjType = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
$ProductSlnGuid = "{97870930-ADA9-4953-8A4F-F33E7B0D625A}"
$VnvSlnGuid = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"

function Read-JsonFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing manifest: $path" }
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}

function Normalize-Guid([string]$g) {
    $g = $g.Trim()
    if (-not $g.StartsWith("{")) { $g = "{$g" }
    if (-not $g.EndsWith("}")) { $g = "$g}" }
    return $g.ToUpperInvariant()
}

function Get-VcxprojGuid([string]$relPath) {
    $full = Join-Path $RepoRoot $relPath
    if (-not (Test-Path -LiteralPath $full)) { throw "vcxproj not found: $relPath" }
    $line = Select-String -Path $full -Pattern "ProjectGuid" | Select-Object -First 1
    if (-not $line) { throw "No ProjectGuid in $relPath" }
    if ($line.Line -match "\{[0-9A-Fa-f-]+\}") {
        return Normalize-Guid $Matches[0]
    }
    throw "Could not parse ProjectGuid in $relPath"
}

function New-SlnProjectBlock {
    param(
        [string]$Name,
        [string]$RelVcxproj,  # path relative to sln/ folder
        [string]$Guid,
        [string[]]$DepGuids = @()
    )
    $lines = @()
    $lines += "Project(`"$CppProjType`") = `"$Name`", `"$RelVcxproj`", `"$Guid`""
    if ($DepGuids -and $DepGuids.Count -gt 0) {
        $lines += "`tProjectSection(ProjectDependencies) = postProject"
        foreach ($d in $DepGuids) {
            $lines += "`t`t$d = $d"
        }
        $lines += "`tEndProjectSection"
    }
    $lines += "EndProject"
    return ($lines -join "`r`n")
}

function New-ConfigLines {
    param(
        [string]$Guid,
        [bool]$HasWin32 = $false,
        [bool]$IsTestX64Only = $false
    )
    $lines = @()
    if ($IsTestX64Only) {
        $lines += "`t`t$Guid.Debug|x64.ActiveCfg = Debug|x64"
        $lines += "`t`t$Guid.Debug|x64.Build.0 = Debug|x64"
        $lines += "`t`t$Guid.Debug|x86.ActiveCfg = Debug|x64"
        $lines += "`t`t$Guid.Release|x64.ActiveCfg = Release|x64"
        $lines += "`t`t$Guid.Release|x64.Build.0 = Release|x64"
        $lines += "`t`t$Guid.Release|x86.ActiveCfg = Release|x64"
    } else {
        $lines += "`t`t$Guid.Debug|x64.ActiveCfg = Debug|x64"
        $lines += "`t`t$Guid.Debug|x64.Build.0 = Debug|x64"
        $lines += "`t`t$Guid.Debug|x86.ActiveCfg = Debug|Win32"
        $lines += "`t`t$Guid.Debug|x86.Build.0 = Debug|Win32"
        $lines += "`t`t$Guid.Release|x64.ActiveCfg = Release|x64"
        $lines += "`t`t$Guid.Release|x64.Build.0 = Release|x64"
        $lines += "`t`t$Guid.Release|x86.ActiveCfg = Release|Win32"
        $lines += "`t`t$Guid.Release|x86.Build.0 = Release|Win32"
    }
    return $lines
}

function Build-SolutionText {
    param(
        [object[]]$Projects,  # @{Name; RelPath; Guid; DepGuids; TestOnly}
        [string]$SolutionGuid
    )
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("Microsoft Visual Studio Solution File, Format Version 12.00")
    [void]$sb.AppendLine("# Visual Studio Version 17")
    [void]$sb.AppendLine("VisualStudioVersion = 17.12.35506.116")
    [void]$sb.AppendLine("MinimumVisualStudioVersion = 10.0.40219.1")
    foreach ($p in $Projects) {
        [void]$sb.AppendLine((New-SlnProjectBlock -Name $p.Name -RelVcxproj $p.RelPath -Guid $p.Guid -DepGuids $p.DepGuids))
    }
    [void]$sb.AppendLine("Global")
    [void]$sb.AppendLine("`tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    [void]$sb.AppendLine("`t`tDebug|x64 = Debug|x64")
    [void]$sb.AppendLine("`t`tDebug|x86 = Debug|x86")
    [void]$sb.AppendLine("`t`tRelease|x64 = Release|x64")
    [void]$sb.AppendLine("`t`tRelease|x86 = Release|x86")
    [void]$sb.AppendLine("`tEndGlobalSection")
    [void]$sb.AppendLine("`tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    foreach ($p in $Projects) {
        $cfg = New-ConfigLines -Guid $p.Guid -IsTestX64Only:([bool]$p.TestOnly)
        foreach ($c in $cfg) { [void]$sb.AppendLine($c) }
    }
    [void]$sb.AppendLine("`tEndGlobalSection")
    [void]$sb.AppendLine("`tGlobalSection(SolutionProperties) = preSolution")
    [void]$sb.AppendLine("`t`tHideSolutionNode = FALSE")
    [void]$sb.AppendLine("`tEndGlobalSection")
    [void]$sb.AppendLine("`tGlobalSection(ExtensibilityGlobals) = postSolution")
    [void]$sb.AppendLine("`t`tSolutionGuid = $SolutionGuid")
    [void]$sb.AppendLine("`tEndGlobalSection")
    [void]$sb.AppendLine("EndGlobal")
    return $sb.ToString()
}

function Write-OrCheck {
    param([string]$Path, [string]$Content)
    $full = Join-Path $RepoRoot $Path
    $normalized = $Content -replace "`r`n", "`n" -replace "`n", "`r`n"
    if ($CheckOnly) {
        if (-not (Test-Path -LiteralPath $full)) {
            Write-Host "CHECK FAIL: missing $Path (would create)"
            return $false
        }
        $existing = [System.IO.File]::ReadAllText($full)
        # Normalize line endings for compare
        $a = ($existing -replace "`r`n", "`n").TrimEnd()
        $b = ($normalized -replace "`r`n", "`n").TrimEnd()
        if ($a -ne $b) {
            Write-Host "CHECK FAIL: $Path differs from generated"
            return $false
        }
        if ($Verbose) { Write-Host "CHECK OK: $Path" }
        return $true
    }
    $dir = Split-Path -Parent $full
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllText($full, $normalized)
    Write-Host "Wrote $Path"
    return $true
}

# --- Load manifests ---
$libsManifest = Read-JsonFile (Join-Path $RepoRoot "vnv/manifest/libraries.json")
$appsManifest = Read-JsonFile (Join-Path $RepoRoot "vnv/manifest/product_apps.json")
$vnvManifest = Read-JsonFile (Join-Path $RepoRoot "vnv/manifest/vnv_projects.json")

$libById = @{}
foreach ($lib in $libsManifest.libraries) {
    $fileGuid = Get-VcxprojGuid $lib.vcxproj
    $manifestGuid = Normalize-Guid $lib.guid
    if ($fileGuid -ne $manifestGuid) {
        throw "GUID mismatch for $($lib.id): manifest $manifestGuid vs file $fileGuid"
    }
    $libById[$lib.id] = $lib
    if ($Verbose) { Write-Host "lib $($lib.id) $manifestGuid" }
}

# Validate depends_on
foreach ($lib in $libsManifest.libraries) {
    foreach ($d in $lib.depends_on) {
        if (-not $libById.ContainsKey($d)) { throw "Unknown depends_on '$d' on library $($lib.id)" }
    }
}

function DepGuidsForIds([string[]]$ids) {
    $out = @()
    foreach ($id in $ids) {
        if ($libById.ContainsKey($id)) {
            $out += (Normalize-Guid $libById[$id].guid)
        }
    }
    return $out
}

# --- Product solution ---
$productProjects = @()
foreach ($lib in $libsManifest.libraries) {
    if (-not $lib.in_product_sln) { continue }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($lib.vcxproj)
    $rel = Split-Path -Leaf $lib.vcxproj
    $productProjects += [pscustomobject]@{
        Name     = $name
        RelPath  = $rel
        Guid     = (Normalize-Guid $lib.guid)
        DepGuids = @(DepGuidsForIds @($lib.depends_on))
        TestOnly = $false
    }
}
foreach ($app in $appsManifest.apps) {
    $fileGuid = Get-VcxprojGuid $app.vcxproj
    $manifestGuid = Normalize-Guid $app.guid
    if ($fileGuid -ne $manifestGuid) {
        throw "GUID mismatch for app $($app.id): manifest $manifestGuid vs file $fileGuid"
    }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($app.vcxproj)
    $rel = Split-Path -Leaf $app.vcxproj
    $productProjects += [pscustomobject]@{
        Name     = $name
        RelPath  = $rel
        Guid     = $manifestGuid
        DepGuids = @(DepGuidsForIds @($app.depends_on))
        TestOnly = $false
    }
}

# Order: libs first (graphics, network, engine), then app — keep stable
$productText = Build-SolutionText -Projects $productProjects -SolutionGuid $ProductSlnGuid

# --- VnV solution ---
$vnvProjects = @()
foreach ($lib in $libsManifest.libraries) {
    if (-not $lib.in_vnv_sln) { continue }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($lib.vcxproj)
    $rel = Split-Path -Leaf $lib.vcxproj
    $vnvProjects += [pscustomobject]@{
        Name     = $name
        RelPath  = $rel
        Guid     = (Normalize-Guid $lib.guid)
        DepGuids = @(DepGuidsForIds @($lib.depends_on))
        TestOnly = $false
    }
}
foreach ($tp in $vnvManifest.projects) {
    $fileGuid = Get-VcxprojGuid $tp.vcxproj
    $manifestGuid = Normalize-Guid $tp.guid
    if ($fileGuid -ne $manifestGuid) {
        throw "GUID mismatch for VnV $($tp.id): manifest $manifestGuid vs file $fileGuid"
    }
    $name = $tp.id
    $rel = Split-Path -Leaf $tp.vcxproj
    $depIds = @()
    if ($tp.depends_on_libraries) { $depIds = @($tp.depends_on_libraries) }
    $vnvProjects += [pscustomobject]@{
        Name     = $name
        RelPath  = $rel
        Guid     = $manifestGuid
        DepGuids = @(DepGuidsForIds $depIds)
        TestOnly = $true
    }
    if ($Verbose) { Write-Host "vnv $($tp.id) category=$($tp.category)" }
}

$vnvText = Build-SolutionText -Projects $vnvProjects -SolutionGuid $VnvSlnGuid

$ok1 = Write-OrCheck -Path "sln/alephium_visualizer.sln" -Content $productText
$ok2 = Write-OrCheck -Path "sln/blockviz_vnv.sln" -Content $vnvText

if ($CheckOnly) {
    if ($ok1 -and $ok2) {
        Write-Host "sync_solutions: CHECK OK (slns match manifests)"
        exit 0
    }
    Write-Host "sync_solutions: CHECK FAILED - run .\scripts\sync_solutions.ps1"
    exit 2
}

Write-Host "sync_solutions: done (product + VnV)"
exit 0
