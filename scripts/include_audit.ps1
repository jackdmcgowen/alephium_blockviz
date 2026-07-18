# Rank #include frequency from product sources (rough IWYU heat map).
# Usage: powershell -ExecutionPolicy Bypass -File scripts/include_audit.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$headers = @{}
Get-ChildItem src -Recurse -Include *.cpp,*.c,*.hpp,*.h |
  Where-Object { $_.FullName -notmatch '\\pch\.(h|cpp)$' } |
  ForEach-Object {
    Select-String -Path $_.FullName -Pattern '^\s*#include\s*[<"]([^>"]+)[>"]' -ErrorAction SilentlyContinue |
      ForEach-Object {
        $h = $_.Matches[0].Groups[1].Value
        if (-not $headers.ContainsKey($h)) { $headers[$h] = 0 }
        $headers[$h]++
      }
  }

Write-Host "Top includes (product tree):"
$headers.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 40 |
  ForEach-Object { "{0,4}  {1}" -f $_.Value, $_.Name }

Write-Host ""
Write-Host "Heavy third-party / fan-out flags:"
@("vulkan/vulkan.h", "glm/glm.hpp", "imgui.h", "curl/curl.h", "windows.h", "engine/engine.hpp", "graphics/graphics_system.hpp") |
  ForEach-Object {
    $c = if ($headers.ContainsKey($_)) { $headers[$_] } else { 0 }
    "{0,4}  {1}" -f $c, $_
  }
