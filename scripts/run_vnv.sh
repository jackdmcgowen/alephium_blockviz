#!/usr/bin/env bash
# Linux VnV runner.
# Parity with: .\scripts\run_vnv.ps1
#
#   ./scripts/run_vnv.sh                 # mod only (default)
#   ./scripts/run_vnv.sh --all           # mod + int (capture + compare if possible)
#   ./scripts/run_vnv.sh --int
#   ./scripts/run_vnv.sh --int --headless   # VK_EXT_headless_surface (no DISPLAY)
#   ./scripts/run_vnv.sh --bench         # opt-in; needs GPU/window (or --headless)
#   ./scripts/run_vnv.sh --bench --mass  # also run fake_mass_2k / fake_mass_4k
#   ./scripts/run_vnv.sh --skip-build
#   ./scripts/run_vnv.sh --config Release
#   ./scripts/run_vnv.sh --ctest         # mod via ctest
#   ./scripts/run_vnv.sh --report        # write vnv/reports/last_run.{json,html}
#   ./scripts/run_vnv.sh --report-only   # regenerate HTML from existing last_run.json
#
# Notes:
# - Prefer --headless for CI/GPU-less capture (swapchain GPU readback PNG).
# - Windowed int needs DISPLAY; goldens remain machine-sensitive.
# - Results JSON + HTML: vnv/reports/ (gitignored); catalog in vnv/manifest/case_catalog.json
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
RUN_MASS=0
UPDATE_GOLDENS=0
UPDATE_BASELINES=0
HEADLESS="${BLOCKVIZ_HEADLESS:-0}"
BENCH_TOL="${BENCH_TOL:-0.15}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
WRITE_REPORT=0
REPORT_ONLY=0
RESULTS_DIR="vnv/reports"
RESULTS_JSON="${RESULTS_DIR}/last_run.json"
RESULTS_HTML="${RESULTS_DIR}/last_run.html"
PREV_JSON="${RESULTS_DIR}/previous_run.json"
CATALOG="vnv/manifest/case_catalog.json"

usage() {
  sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
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
    --mass) RUN_MASS=1; shift ;;
    --all) RUN_MOD=1; RUN_INT=1; RUN_BENCH=0; shift ;;
    --headless) HEADLESS=1; shift ;;
    --update-goldens) UPDATE_GOLDENS=1; shift ;;
    --update-baselines) UPDATE_BASELINES=1; shift ;;
    --report) WRITE_REPORT=1; shift ;;
    --report-only) REPORT_ONLY=1; WRITE_REPORT=1; shift ;;
    --results-json) RESULTS_JSON="${2:-}"; shift 2 ;;
    --results-html) RESULTS_HTML="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *)
      echo "unknown arg: $1 (try --help)"
      exit 2
      ;;
  esac
done

# --mass alone implies bench
if [[ "$RUN_MASS" -eq 1 && "$RUN_BENCH" -eq 0 && "$RUN_INT" -eq 0 && "$REPORT_ONLY" -eq 0 ]]; then
  # If user only passed --mass (maybe with --mod default), enable bench.
  if [[ "$RUN_MOD" -eq 1 && "$RUN_INT" -eq 0 ]]; then
    # keep mod if default, but also enable bench when --mass used with default
    :
  fi
  RUN_BENCH=1
fi

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

# ---------------------------------------------------------------------------
# Results accumulation (temp file → final JSON)
# ---------------------------------------------------------------------------
RESULTS_TMP=""
cleanup_tmp() {
  if [[ -n "${RESULTS_TMP}" && -f "${RESULTS_TMP}" ]]; then
    rm -f "${RESULTS_TMP}"
  fi
}
trap cleanup_tmp EXIT

init_results() {
  mkdir -p "${RESULTS_DIR}"
  RESULTS_TMP="$(mktemp "${TMPDIR:-/tmp}/vnv_results.XXXXXX")"
  : > "${RESULTS_TMP}"
}

# Append one case line: id|status|suite|kind|message|expected|actual|diff|report|metrics_json_or_empty
record_case() {
  local id="$1" status="$2" suite="$3" kind="$4" message="${5:-}"
  local expected="${6:-}" actual="${7:-}" diff="${8:-}" report="${9:-}" metrics="${10:-}"
  # Escape pipes in fields
  message="${message//|/\\|}"
  printf '%s\n' "${id}|${status}|${suite}|${kind}|${message}|${expected}|${actual}|${diff}|${report}|${metrics}" \
    >> "${RESULTS_TMP}"
}

git_sha() {
  git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo ""
}

write_results_json() {
  local overall="pass"
  if [[ "$failures" -gt 0 ]]; then
    overall="fail"
  fi
  local sha
  sha="$(git_sha)"
  local generated
  generated="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

  # Rotate previous
  if [[ -f "${RESULTS_JSON}" ]]; then
    cp -f "${RESULTS_JSON}" "${PREV_JSON}"
  fi

  python3 - "$RESULTS_TMP" "$RESULTS_JSON" "$overall" "$generated" "$sha" "$CONFIG" "$HEADLESS" <<'PY'
import json, sys
from pathlib import Path

tmp, out_p, overall, generated, sha, config, headless = sys.argv[1:8]
cases = []
suites = set()
for line in Path(tmp).read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    parts = line.split("|", 9)
    while len(parts) < 10:
        parts.append("")
    cid, status, suite, kind, message, expected, actual, diff, report, metrics = parts
    suites.add(suite)
    entry = {
        "id": cid,
        "status": status,
        "suite": suite,
        "kind": kind,
        "message": message.replace("\\|", "|"),
        "expected": expected or None,
        "actual": actual or None,
        "diff": diff or None,
        "report": report or None,
        "headless": headless in ("1", "true", "True"),
    }
    if metrics:
        try:
            entry["metrics"] = json.loads(metrics)
        except json.JSONDecodeError:
            entry["metrics_raw"] = metrics
    # Drop nulls for cleanliness (keep expected/actual even if null for schema clarity)
    cases.append(entry)

doc = {
    "version": 1,
    "status": overall,
    "generated_at": generated,
    "git_sha": sha,
    "config": config,
    "headless": headless in ("1", "true", "True"),
    "suites": sorted(s for s in suites if s),
    "cases": cases,
}
Path(out_p).parent.mkdir(parents=True, exist_ok=True)
Path(out_p).write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
print(f"Wrote {out_p} ({len(cases)} cases, status={overall})")
PY
}

generate_html_report() {
  local before_args=()
  if [[ -f "${PREV_JSON}" ]]; then
    before_args=(--before "${PREV_JSON}")
  fi
  python3 scripts/generate_vnv_report.py \
    --results "${RESULTS_JSON}" \
    --catalog "${CATALOG}" \
    --out "${RESULTS_HTML}" \
    "${before_args[@]}"
}

# ---------------------------------------------------------------------------
# report-only fast path
# ---------------------------------------------------------------------------
if [[ "$REPORT_ONLY" -eq 1 ]]; then
  if [[ ! -f "${RESULTS_JSON}" ]]; then
    echo "ERROR: no results at ${RESULTS_JSON}; run VnV with --report first"
    exit 2
  fi
  generate_html_report
  exit 0
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
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
init_results

run_mod_one() {
  local id="$1"
  local path
  path="$(bin_path "$id")"
  echo ""
  echo "== VnV [mod] ${id} =="
  if [[ -z "$path" ]]; then
    echo "FAIL: missing executable for ${id}"
    failures=$((failures + 1))
    record_case "$id" "fail" "mod" "exit_code" "missing executable"
    return
  fi
  if [[ "$USE_CTEST" -eq 1 ]]; then
    if (cd "${BUILD_DIR}" && ctest -R "^${id}$" --output-on-failure -C "${CONFIG}"); then
      echo "PASS: ${id}"
      record_case "$id" "pass" "mod" "exit_code" "ctest ok"
    else
      failures=$((failures + 1))
      record_case "$id" "fail" "mod" "exit_code" "ctest failed"
    fi
    return
  fi
  if "$path"; then
    echo "PASS: ${id}"
    record_case "$id" "pass" "mod" "exit_code" "exit 0"
  else
    echo "FAIL: ${id} exited non-zero"
    failures=$((failures + 1))
    record_case "$id" "fail" "mod" "exit_code" "non-zero exit"
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
  # Keep in sync with int_visual --list / vnv_projects.json cases[] / case_catalog.json
  INT_CASES=(fake_overview fake_side_cam fake_selection_sobel)
  path="$(bin_path int_visual)"
  if [[ -z "$path" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: missing int_visual (build with -DBLOCKVIZ_BUILD_VNV_GPU=ON)"
    failures=$((failures + 1))
    record_case "int_visual" "fail" "int" "visual_golden" "missing int_visual"
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    failures=$((failures + 1))
    record_case "int_visual" "fail" "int" "visual_golden" "no display"
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
        record_case "$case" "fail" "int" "visual_golden" "capture failed" "$golden" "$actual" "$diff" "$report"
      elif [[ "$UPDATE_GOLDENS" -eq 1 ]]; then
        mkdir -p "$(dirname "$golden")"
        cp -f "$actual" "$golden"
        echo "PASS: updated golden $golden"
        record_case "$case" "pass" "int" "visual_golden" "updated golden" "$golden" "$actual" "" ""
      else
        if [[ ! -f "$golden" ]]; then
          if python3 vnv/int/tests/visual/compare_images.py \
              --actual "$actual" --profile smoke \
              --report-out "$report"; then
            echo "PASS: int_visual smoke (no golden at $golden — commit with --update-goldens)"
            record_case "$case" "pass" "int" "visual_golden" "smoke (no golden)" "$golden" "$actual" "$diff" "$report"
          else
            echo "FAIL: int_visual smoke (see $report)"
            failures=$((failures + 1))
            record_case "$case" "fail" "int" "visual_golden" "smoke failed" "$golden" "$actual" "$diff" "$report"
          fi
        elif python3 vnv/int/tests/visual/compare_images.py \
            --expected "$golden" --actual "$actual" \
            --profile "$compare_profile" \
            --diff-out "$diff" --report-out "$report"; then
          echo "PASS: int_visual ${case} (${compare_profile})"
          record_case "$case" "pass" "int" "visual_golden" "compare ok (${compare_profile})" "$golden" "$actual" "$diff" "$report"
        else
          echo "FAIL: int_visual compare (see $report)"
          failures=$((failures + 1))
          record_case "$case" "fail" "int" "visual_golden" "compare failed" "$golden" "$actual" "$diff" "$report"
        fi
      fi
    done
  fi
fi

if [[ "$RUN_BENCH" -eq 1 ]]; then
  # Default bench cases; --mass adds multi-thousand-block scenes.
  BENCH_CASES=(fake_steady_frame)
  if [[ "$RUN_MASS" -eq 1 ]]; then
    BENCH_CASES+=(fake_mass_2k fake_mass_4k)
  fi
  # Allow override: BENCH_CASES="fake_steady_frame fake_mass_2k" ./scripts/run_vnv.sh --bench --skip-build
  if [[ -n "${BENCH_CASES_OVERRIDE:-}" ]]; then
    # shellcheck disable=SC2206
    BENCH_CASES=($BENCH_CASES_OVERRIDE)
  fi

  path="$(bin_path bench_frame_profiler)"
  if [[ -z "$path" ]]; then
    echo ""
    echo "== VnV [bench] bench_frame_profiler =="
    echo "FAIL: missing bench_frame_profiler"
    failures=$((failures + 1))
    record_case "bench_frame_profiler" "fail" "bench" "perf_baseline" "missing binary"
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo ""
    echo "== VnV [bench] bench_frame_profiler =="
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    failures=$((failures + 1))
    record_case "bench_frame_profiler" "fail" "bench" "perf_baseline" "no display"
  else
    for case in "${BENCH_CASES[@]}"; do
      out_dir="vnv/bench/tests/out/${case}"
      actual="${out_dir}/actual.json"
      baseline="vnv/bench/baselines/${case}.json"
      echo ""
      echo "== VnV [bench] bench_frame_profiler (${case}) headless=${HEADLESS} =="
      mkdir -p "$out_dir"
      if ! "$path" --case "$case" --out "$actual" "${HEADLESS_ARGS[@]}"; then
        echo "FAIL: bench capture (${case})"
        failures=$((failures + 1))
        metrics=""
        if [[ -f "$actual" ]]; then
          metrics="$(python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))))" "$actual" 2>/dev/null || true)"
        fi
        record_case "$case" "fail" "bench" "perf_baseline" "capture/confidence failed" "$baseline" "$actual" "" "" "$metrics"
      elif [[ "$UPDATE_BASELINES" -eq 1 ]]; then
        mkdir -p "$(dirname "$baseline")"
        cp -f "$actual" "$baseline"
        echo "PASS: updated baseline $baseline"
        metrics="$(python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))))" "$actual")"
        record_case "$case" "pass" "bench" "perf_baseline" "updated baseline" "$baseline" "$actual" "" "" "$metrics"
      else
        metrics="$(python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))))" "$actual")"
        if [[ ! -f "$baseline" ]]; then
          # Soft-pass when no baseline yet (mass cases may not be baselined on every machine)
          conf="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('confidence',1))" "$actual")"
          echo "PASS: bench ${case} (no baseline — confidence=${conf}; commit with --update-baselines)"
          record_case "$case" "pass" "bench" "perf_baseline" "no baseline; soft confidence gate only" "$baseline" "$actual" "" "" "$metrics"
        elif python3 - "$baseline" "$actual" "$BENCH_TOL" <<'PY'
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
# Optional: total_blocks sanity for mass cases
if "total_blocks" in act and "total_blocks" in base:
    # Allow growth from live ticks; require at least 80% of baseline block count
    if float(act["total_blocks"]) < float(base["total_blocks"]) * 0.8:
        print(f"FAIL: total_blocks {act['total_blocks']} << baseline {base['total_blocks']}")
        ok = False
    else:
        print(f"  ok total_blocks: actual={act['total_blocks']} baseline={base['total_blocks']}")
sys.exit(0 if ok else 1)
PY
        then
          echo "PASS: bench ${case}"
          record_case "$case" "pass" "bench" "perf_baseline" "baseline compare ok" "$baseline" "$actual" "" "" "$metrics"
        else
          echo "FAIL: bench baseline compare (${case})"
          failures=$((failures + 1))
          record_case "$case" "fail" "bench" "perf_baseline" "baseline compare failed" "$baseline" "$actual" "" "" "$metrics"
        fi
      fi
    done
  fi
fi

# Always write results JSON when any suite ran; HTML when --report
write_results_json
if [[ "$WRITE_REPORT" -eq 1 ]]; then
  generate_html_report
fi

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "VnV: all selected suites passed"
  if [[ "$WRITE_REPORT" -eq 1 ]]; then
    echo "VnV report: ${RESULTS_HTML}"
  else
    echo "VnV results: ${RESULTS_JSON} (pass --report for HTML)"
  fi
  exit 0
fi
echo "VnV: ${failures} suite(s) failed"
if [[ "$WRITE_REPORT" -eq 1 ]]; then
  echo "VnV report: ${RESULTS_HTML}"
fi
exit 1
