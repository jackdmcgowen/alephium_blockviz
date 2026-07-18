# Creates annotated app-v* / engine-v* tags on the current main tip from identity headers.
# Usage (repo root):  powershell -File scripts/tag-main-release.ps1 [-Push]
param(
    [switch]$Push
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

function Read-Version([string]$path, [string]$nsHint) {
    $text = Get-Content $path -Raw
    if ($text -notmatch 'kVersionMajor\s*=\s*(\d+)') { throw "no major in $path" }
    $maj = [int]$Matches[1]
    if ($text -notmatch 'kVersionMinor\s*=\s*(\d+)') { throw "no minor in $path" }
    $min = [int]$Matches[1]
    if ($text -notmatch 'kVersionPatch\s*=\s*(\d+)') { throw "no patch in $path" }
    $pat = [int]$Matches[1]
    return "$maj.$min.$pat"
}

$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") {
    Write-Host "Checking out main (was $branch)..."
    git checkout main
    git pull origin main
}

$appVer = Read-Version "src/app/app_identity.hpp" "app"
$engVer = Read-Version "src/engine/engine_identity.hpp" "engine"
$appTag = "app-v$appVer"
$engTag = "engine-v$engVer"
$head = (git rev-parse HEAD).Trim()

Write-Host "main tip: $head"
Write-Host "tags:     $appTag  $engTag"

if (git rev-parse -q --verify "refs/tags/$appTag" 2>$null) {
    $t = (git rev-list -n1 $appTag).Trim()
    if ($t -ne $head) { throw "Tag $appTag already exists on $t (main is $head)" }
    Write-Host "$appTag already on main tip"
} else {
    git tag -a $appTag -m "Alephium BlockFlow $appVer — host application version"
    Write-Host "Created $appTag"
}

if (git rev-parse -q --verify "refs/tags/$engTag" 2>$null) {
    $t = (git rev-list -n1 $engTag).Trim()
    if ($t -ne $head) { throw "Tag $engTag already exists on $t (main is $head)" }
    Write-Host "$engTag already on main tip"
} else {
    git tag -a $engTag -m "BlockvizEngine $engVer — ISystem registry / product surface"
    Write-Host "Created $engTag"
}

if ($Push) {
    git push origin $appTag $engTag
    Write-Host "Pushed $appTag $engTag"
} else {
    Write-Host "Dry run complete. Re-run with -Push to publish tags."
}
