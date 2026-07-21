#!/usr/bin/env bash
# Dual-track smoke before merging platform/Linux work.
#
# Linux (this script):
#   python3 scripts/check_pch.py
#   build (optional) + mod VnV
#   headless int_visual PNG size + optional golden compare
#
# Windows (run manually on a VS machine):
#   python3 scripts/check_pch.py
#   Open sln/alephium_visualizer.sln → Debug|x64 build
#   .\scripts\run_vnv.ps1
#   Launch product from repo root; Esc quit clean
#
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BLOCKVIZ_BUILD_DIR:-build}"
SKIP_BUILD=0
HEADLESS=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --no-headless) HEADLESS=0; shift ;;
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *) echo "unknown arg: $1"; exit 2 ;;
  esac
done

echo "======== dual-track smoke (Linux) ========"
echo "repo: $REPO_ROOT"

echo ""
echo "== PCH audit =="
python3 scripts/check_pch.py

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "FAIL: no CMake build at ${BUILD_DIR}/ — configure first (see docs/linux.md)"
    exit 1
  fi
  echo ""
  echo "== build mod + int_visual =="
  cmake --build "${BUILD_DIR}" --target mod_domain mod_network int_visual -j"$(nproc 2>/dev/null || echo 4)"
fi

echo ""
echo "== mod VnV =="
./scripts/run_vnv.sh --skip-build

if [[ "$HEADLESS" -eq 1 ]]; then
  echo ""
  echo "== headless int_visual =="
  # Prefer lavapipe when present (CI / GPU-less)
  if [[ -z "${VK_ICD_FILENAMES:-}" && -f /usr/share/vulkan/icd.d/lvp_icd.json ]]; then
    export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
    echo "using VK_ICD_FILENAMES=$VK_ICD_FILENAMES"
  fi
  ./scripts/run_vnv.sh --skip-build --int --headless
fi

echo ""
echo "======== Linux smoke PASSED ========"
echo ""
echo "Windows checklist (manual):"
echo "  1. python3 scripts/check_pch.py"
echo "  2. MSBuild sln/alephium_visualizer.sln Debug|x64 (or VS)"
echo "  3. .\\scripts\\run_vnv.ps1"
echo "  4. Run product from repo root; smoke Esc quit"
exit 0
