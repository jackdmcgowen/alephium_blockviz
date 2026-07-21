#!/usr/bin/env bash
# Linux VnV runner — mod (CPU) only for Phase 3.
# Parity with: .\scripts\run_vnv.ps1  (default mod)
#
#   ./scripts/run_vnv.sh
#   ./scripts/run_vnv.sh --skip-build
#   ./scripts/run_vnv.sh --config Release
#   ./scripts/run_vnv.sh --ctest          # run via ctest after build
#
# int / bench: not yet ported (use Windows run_vnv.ps1 -Int / -Bench).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BLOCKVIZ_BUILD_DIR:-build}"
CONFIG="Debug"
SKIP_BUILD=0
USE_CTEST=0
GENERATOR="${CMAKE_GENERATOR:-Ninja}"

usage() {
  sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --config) CONFIG="${2:-Debug}"; shift 2 ;;
    --ctest) USE_CTEST=1; shift ;;
    --build-dir) BUILD_DIR="${2:-build}"; shift 2 ;;
    -h|--help) usage ;;
    --all|--int|--bench)
      echo "note: $1 not implemented on Linux yet (mod only). Continuing with mod."
      shift
      ;;
    *)
      echo "unknown arg: $1 (try --help)"
      exit 2
      ;;
  esac
done

bin_path() {
  local name="$1"
  # single-config (Ninja/Makefile)
  if [[ -x "${BUILD_DIR}/bin/${name}" ]]; then
    echo "${BUILD_DIR}/bin/${name}"
    return
  fi
  # multi-config (VS generators)
  if [[ -x "${BUILD_DIR}/bin/${CONFIG}/${name}" ]]; then
    echo "${BUILD_DIR}/bin/${CONFIG}/${name}"
    return
  fi
  if [[ -x "${BUILD_DIR}/${name}" ]]; then
    echo "${BUILD_DIR}/${name}"
    return
  fi
  echo ""
}

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "== cmake configure (${BUILD_DIR}, ${GENERATOR}) =="
    CMAKE_ARGS=(
      -S .
      -B "${BUILD_DIR}"
      -G "${GENERATOR}"
      -DCMAKE_BUILD_TYPE="${CONFIG}"
      -DBLOCKVIZ_BUILD_VNV=ON
    )
    # Optional toolchain / prefix from environment (docs/linux.md)
    if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
      CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}")
    elif [[ -f vcpkg/scripts/buildsystems/vcpkg.cmake ]]; then
      # only auto-wire if user already has vcpkg install tree
      if [[ -d vcpkg/installed/x64-linux || -d vcpkg/installed/x64-windows ]]; then
        CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        CMAKE_ARGS+=(-DVCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}")
      fi
    fi
    if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
      CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}")
    fi
    cmake "${CMAKE_ARGS[@]}"
  else
    # ensure VnV option on if reusing an existing tree
    cmake -S . -B "${BUILD_DIR}" -DBLOCKVIZ_BUILD_VNV=ON >/dev/null
  fi

  echo "== build mod_domain mod_network =="
  cmake --build "${BUILD_DIR}" --target mod_domain mod_network --config "${CONFIG}"
fi

failures=0

run_one() {
  local id="$1"
  local path
  path="$(bin_path "$id")"
  echo ""
  echo "== VnV [mod] ${id} =="
  if [[ -z "$path" ]]; then
    echo "FAIL: missing executable for ${id} under ${BUILD_DIR}/bin/"
    failures=$((failures + 1))
    return
  fi
  if [[ "$USE_CTEST" -eq 1 ]]; then
    if ! (cd "${BUILD_DIR}" && ctest -R "^${id}$" --output-on-failure -C "${CONFIG}"); then
      failures=$((failures + 1))
    fi
    return
  fi
  if ! "$path"; then
    echo "FAIL: ${id} exited non-zero"
    failures=$((failures + 1))
  else
    echo "PASS: ${id}"
  fi
}

run_one mod_domain
run_one mod_network

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "VnV mod: all passed"
  exit 0
fi
echo "VnV mod: ${failures} suite(s) failed"
exit 1
