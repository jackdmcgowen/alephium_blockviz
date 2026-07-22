#!/usr/bin/env bash
# Linux VnV runner.
# Parity with: .\scripts\run_vnv.ps1
#
#   ./scripts/run_vnv.sh                 # mod only (default)
#   ./scripts/run_vnv.sh --all           # mod + int (capture + compare if possible)
#   ./scripts/run_vnv.sh --int
#   ./scripts/run_vnv.sh --int --headless   # VK_EXT_headless_surface (no DISPLAY)
#   ./scripts/run_vnv.sh --bench         # opt-in; needs GPU/window (or --headless)
#   ./scripts/run_vnv.sh --skip-build
#   ./scripts/run_vnv.sh --config Release
#   ./scripts/run_vnv.sh --ctest         # mod via ctest
#
# Notes:
# - Prefer --headless for CI/GPU-less capture (swapchain GPU readback PNG).
# - Windowed int needs DISPLAY; goldens remain machine-sensitive.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BLOCKVIZ_BUILD_DIR:-build}"
CONFIG="Debug"
SKIP_BUILD=0
USE_CTEST=0
RUN_MOD=1
RUN_INT=0
RUN_BENCH=0
UPDATE_GOLDENS=0
UPDATE_BASELINES=0
HEADLESS="${BLOCKVIZ_HEADLESS:-0}"
BENCH_TOL="${BENCH_TOL:-0.15}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"

usage() {
  sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --config) CONFIG="${2:-Debug}"; shift 2 ;;
    --ctest) USE_CTEST=1; shift ;;
    --build-dir) BUILD_DIR="${2:-build}"; shift 2 ;;
    --mod) RUN_MOD=1; shift ;;
    --int) RUN_INT=1; RUN_MOD=0; shift ;;
    --bench) RUN_BENCH=1; RUN_MOD=0; shift ;;
    --all) RUN_MOD=1; RUN_INT=1; RUN_BENCH=0; shift ;;
    --headless) HEADLESS=1; shift ;;
    --update-goldens) UPDATE_GOLDENS=1; shift ;;
    --update-baselines) UPDATE_BASELINES=1; shift ;;
    -h|--help) usage ;;
    *)
      echo "unknown arg: $1 (try --help)"
      exit 2
      ;;
  esac
done

# Allow combining: --mod --int
# If only --int was used, RUN_MOD=0; if user also passed --mod earlier ok.
# Re-parse: if --all already set both. Fine.

bin_path() {
  local name="$1"
  if [[ -x "${BUILD_DIR}/bin/${name}" ]]; then
    echo "${BUILD_DIR}/bin/${name}"
    return
  fi
  if [[ -x "${BUILD_DIR}/bin/${CONFIG}/${name}" ]]; then
    echo "${BUILD_DIR}/bin/${CONFIG}/${name}"
    return
  fi
  echo ""
}

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  targets=(mod_domain mod_network)
  cmake_gpu=OFF
  if [[ "$RUN_INT" -eq 1 || "$RUN_BENCH" -eq 1 ]]; then
    cmake_gpu=ON
    targets+=(int_visual bench_frame_profiler)
  fi

  if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "== cmake configure (${BUILD_DIR}, ${GENERATOR}) =="
    CMAKE_ARGS=(
      -S .
      -B "${BUILD_DIR}"
      -G "${GENERATOR}"
      -DCMAKE_BUILD_TYPE="${CONFIG}"
      -DBLOCKVIZ_BUILD_VNV=ON
      -DBLOCKVIZ_BUILD_VNV_GPU="${cmake_gpu}"
    )
    if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
      CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}")
    elif [[ -f vcpkg/scripts/buildsystems/vcpkg.cmake ]]; then
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
    cmake -S . -B "${BUILD_DIR}" \
      -DBLOCKVIZ_BUILD_VNV=ON \
      -DBLOCKVIZ_BUILD_VNV_GPU="${cmake_gpu}" >/dev/null
  fi

  echo "== build ${targets[*]} =="
  cmake --build "${BUILD_DIR}" --target "${targets[@]}" --config "${CONFIG}"
fi

failures=0

run_mod_one() {
  local id="$1"
  local path
  path="$(bin_path "$id")"
  echo ""
  echo "== VnV [mod] ${id} =="
  if [[ -z "$path" ]]; then
    echo "FAIL: missing executable for ${id}"
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

if [[ "$RUN_MOD" -eq 1 ]]; then
  run_mod_one mod_domain
  run_mod_one mod_network
fi

HEADLESS_ARGS=()
if [[ "$HEADLESS" == "1" || "$HEADLESS" == "true" ]]; then
  HEADLESS_ARGS=(--headless)
fi

if [[ "$RUN_INT" -eq 1 ]]; then
  # Keep in sync with int_visual --list / vnv_projects.json cases[]
  INT_CASES=(fake_overview fake_side_cam fake_selection_sobel)
  path="$(bin_path int_visual)"
  if [[ -z "$path" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: missing int_visual (build with -DBLOCKVIZ_BUILD_VNV_GPU=ON)"
    failures=$((failures + 1))
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    failures=$((failures + 1))
  else
    for case in "${INT_CASES[@]}"; do
      out_dir="vnv/int/tests/visual/out/${case}"
      actual="${out_dir}/actual.png"
      # Multi-platform goldens: see vnv/int/tests/visual/goldens/README.md
      if [[ "$HEADLESS" == "1" || "$HEADLESS" == "true" ]]; then
        golden="vnv/int/tests/visual/goldens/linux_headless/${case}.png"
        compare_profile="headless"
      else
        golden="vnv/int/tests/visual/goldens/${case}.png"
        compare_profile="desktop"
      fi
      diff="${out_dir}/diff.png"
      report="${out_dir}/report.txt"
      echo ""
      echo "== VnV [int] int_visual (${case}) headless=${HEADLESS} golden=${golden} =="
      mkdir -p "$out_dir"
      rm -f "$actual"
      if ! "$path" --case "$case" --out "$actual" "${HEADLESS_ARGS[@]}"; then
        echo "FAIL: int_visual capture (${case})"
        failures=$((failures + 1))
      elif [[ "$UPDATE_GOLDENS" -eq 1 ]]; then
        mkdir -p "$(dirname "$golden")"
        cp -f "$actual" "$golden"
        echo "PASS: updated golden $golden"
      else
        if [[ ! -f "$golden" ]]; then
          if python3 vnv/int/tests/visual/compare_images.py \
              --actual "$actual" --profile smoke \
              --report-out "$report"; then
            echo "PASS: int_visual smoke (no golden at $golden — commit with --update-goldens)"
          else
            echo "FAIL: int_visual smoke (see $report)"
            failures=$((failures + 1))
          fi
        elif python3 vnv/int/tests/visual/compare_images.py \
            --expected "$golden" --actual "$actual" \
            --profile "$compare_profile" \
            --diff-out "$diff" --report-out "$report"; then
          echo "PASS: int_visual ${case} (${compare_profile})"
        else
          echo "FAIL: int_visual compare (see $report)"
          failures=$((failures + 1))
        fi
      fi
    done
  fi
fi

if [[ "$RUN_BENCH" -eq 1 ]]; then
  case="fake_steady_frame"
  out_dir="vnv/bench/tests/out/${case}"
  actual="${out_dir}/actual.json"
  baseline="vnv/bench/baselines/${case}.json"
  path="$(bin_path bench_frame_profiler)"
  echo ""
  echo "== VnV [bench] bench_frame_profiler (${case}) headless=${HEADLESS} =="
  if [[ -z "$path" ]]; then
    echo "FAIL: missing bench_frame_profiler"
    failures=$((failures + 1))
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    failures=$((failures + 1))
  else
    mkdir -p "$out_dir"
    if ! "$path" --case "$case" --out "$actual" "${HEADLESS_ARGS[@]}"; then
      echo "FAIL: bench capture"
      failures=$((failures + 1))
    elif [[ "$UPDATE_BASELINES" -eq 1 ]]; then
      mkdir -p "$(dirname "$baseline")"
      cp -f "$actual" "$baseline"
      echo "PASS: updated baseline $baseline"
    else
      if python3 - "$baseline" "$actual" "$BENCH_TOL" <<'PY'
import json, sys
base_p, act_p, tol = sys.argv[1], sys.argv[2], float(sys.argv[3])
try:
    base = json.load(open(base_p))
    act = json.load(open(act_p))
except Exception as e:
    print("FAIL:", e)
    sys.exit(1)
ok = True
def check(label, b, a):
    global ok
    if b is None:
        return
    limit = float(b) * (1.0 + tol)
    if float(a) > limit:
        print(f"FAIL: {label} median {a:.3f} > baseline {b:.3f}*(1+{tol})={limit:.3f}")
        ok = False
    else:
        print(f"  ok {label}: actual={float(a):.3f} baseline={float(b):.3f}")
check("frame_ms", base.get("frame_ms", {}).get("median"), act.get("frame_ms", {}).get("median"))
check("cpu_ms", base.get("cpu_ms", {}).get("median"), act.get("cpu_ms", {}).get("median"))
check("gpu_ms", base.get("gpu_ms", {}).get("median"), act.get("gpu_ms", {}).get("median"))
sys.exit(0 if ok else 1)
PY
      then
        echo "PASS: bench ${case}"
      else
        echo "FAIL: bench baseline compare"
        failures=$((failures + 1))
      fi
    fi
  fi
fi

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "VnV: all selected suites passed"
  exit 0
fi
echo "VnV: ${failures} suite(s) failed"
exit 1
