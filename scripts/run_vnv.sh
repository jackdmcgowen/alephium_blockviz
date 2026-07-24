#!/usr/bin/env bash
# Linux VnV runner.
# Parity with: .\scripts\run_vnv.ps1
#
#   ./scripts/run_vnv.sh                 # mod only (default); always writes report
#   ./scripts/run_vnv.sh --all           # mod + int
#   ./scripts/run_vnv.sh --int --headless
#   ./scripts/run_vnv.sh --bench --mass
#   ./scripts/run_vnv.sh --skip-build
#   ./scripts/run_vnv.sh --report-only --suite mod   # regenerate HTML+zip for latest mod
#
# Reports (uniform layout — see vnv/reports/README.md):
#   vnv/reports/{mod,int,bench}/<run_id>/
#     index.html  run.json  previous.json  artifacts.zip  artifacts/cases/...
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
# shellcheck source=vnv_report_layout.sh
source "${REPO_ROOT}/scripts/vnv_report_layout.sh"

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
REPORT_ONLY=0
REPORT_ONLY_SUITE="mod"
CATALOG="vnv/manifest/case_catalog.json"

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
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
    --report) shift ;; # always on; kept for CLI compat
    --report-only) REPORT_ONLY=1; shift ;;
    --suite) REPORT_ONLY_SUITE="${2:-mod}"; shift 2 ;;
    -h|--help) usage ;;
    *)
      echo "unknown arg: $1 (try --help)"
      exit 2
      ;;
  esac
done

if [[ "$RUN_MASS" -eq 1 && "$RUN_BENCH" -eq 0 && "$RUN_INT" -eq 0 && "$REPORT_ONLY" -eq 0 ]]; then
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
# Per-suite results accumulation
# ---------------------------------------------------------------------------
RESULTS_TMP=""
SUITE_FAILURES=0
cleanup_tmp() {
  if [[ -n "${RESULTS_TMP}" && -f "${RESULTS_TMP}" ]]; then
    rm -f "${RESULTS_TMP}"
  fi
}
trap cleanup_tmp EXIT

suite_begin() {
  local suite="$1"
  vnv_report_begin_run "$suite"
  RESULTS_TMP="$(mktemp "${TMPDIR:-/tmp}/vnv_results.XXXXXX")"
  : > "${RESULTS_TMP}"
  SUITE_FAILURES=0
}

record_case() {
  local id="$1" status="$2" suite="$3" kind="$4" message="${5:-}"
  local expected="${6:-}" actual="${7:-}" diff="${8:-}" report="${9:-}" metrics="${10:-}"
  message="${message//|/\\|}"
  printf '%s\n' "${id}|${status}|${suite}|${kind}|${message}|${expected}|${actual}|${diff}|${report}|${metrics}" \
    >> "${RESULTS_TMP}"
}

suite_finalize() {
  local overall="pass"
  if [[ "$SUITE_FAILURES" -gt 0 ]]; then
    overall="fail"
  fi
  local sha generated
  sha="$(vnv_report_git_short)"
  generated="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

  python3 - "$RESULTS_TMP" "$REPORT_JSON" "$overall" "$generated" "$sha" "$CONFIG" "$HEADLESS" \
    "$REPORT_SUITE" "$REPORT_RUN_ID" <<'PY'
import json, sys
from pathlib import Path
tmp, out_p, overall, generated, sha, config, headless, suite, run_id = sys.argv[1:10]
cases = []
for line in Path(tmp).read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    parts = line.split("|", 9)
    while len(parts) < 10:
        parts.append("")
    cid, status, suite_c, kind, message, expected, actual, diff, report, metrics = parts
    entry = {
        "id": cid,
        "status": status,
        "suite": suite_c,
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
    cases.append(entry)

doc = {
    "version": 2,
    "suite": suite,
    "run_id": run_id,
    "status": overall,
    "generated_at": generated,
    "git_sha": sha,
    "config": config,
    "headless": headless in ("1", "true", "True"),
    "suites": [suite.split("/")[0]],
    "cases": cases,
}
Path(out_p).parent.mkdir(parents=True, exist_ok=True)
Path(out_p).write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
print(f"Wrote {out_p} ({len(cases)} cases, status={overall})")
PY

  local before_args=()
  if [[ -f "${REPORT_PREV}" ]]; then
    before_args=(--before "${REPORT_PREV}")
  fi
  python3 scripts/generate_vnv_report.py \
    --run-dir "${REPORT_RUN_DIR}" \
    --results "${REPORT_JSON}" \
    --catalog "${CATALOG}" \
    --out "${REPORT_HTML}" \
    "${before_args[@]}"
  vnv_report_finalize
  echo "VnV report: ${REPORT_HTML}"
  echo "VnV zip:    ${REPORT_ZIP}"
}

# ---------------------------------------------------------------------------
# report-only
# ---------------------------------------------------------------------------
if [[ "$REPORT_ONLY" -eq 1 ]]; then
  local_dir="$(vnv_report_suite_dir "$REPORT_ONLY_SUITE")/latest"
  if [[ ! -f "${local_dir}/run.json" ]]; then
    echo "ERROR: no report at ${local_dir}/run.json"
    exit 2
  fi
  REPORT_RUN_DIR="$local_dir"
  REPORT_JSON="${local_dir}/run.json"
  REPORT_HTML="${local_dir}/index.html"
  REPORT_PREV="${local_dir}/previous.json"
  REPORT_ZIP="${local_dir}/artifacts.zip"
  REPORT_SUITE="$REPORT_ONLY_SUITE"
  before_args=()
  [[ -f "$REPORT_PREV" ]] && before_args=(--before "$REPORT_PREV")
  python3 scripts/generate_vnv_report.py \
    --run-dir "$REPORT_RUN_DIR" --results "$REPORT_JSON" \
    --catalog "$CATALOG" --out "$REPORT_HTML" "${before_args[@]}" --zip
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
      -S . -B "${BUILD_DIR}" -G "${GENERATOR}"
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
REPORT_PATHS=()

HEADLESS_ARGS=()
if [[ "$HEADLESS" == "1" || "$HEADLESS" == "true" ]]; then
  HEADLESS_ARGS=(--headless)
fi

# ---------------------------------------------------------------------------
# mod
# ---------------------------------------------------------------------------
if [[ "$RUN_MOD" -eq 1 ]]; then
  suite_begin mod
  run_mod_one() {
    local id="$1"
    local path
    path="$(bin_path "$id")"
    echo ""
    echo "== VnV [mod] ${id} =="
    mkdir -p "${REPORT_ARTIFACTS_DIR}/cases/${id}"
    if [[ -z "$path" ]]; then
      echo "FAIL: missing executable for ${id}"
      SUITE_FAILURES=$((SUITE_FAILURES + 1))
      failures=$((failures + 1))
      record_case "$id" "fail" "mod" "exit_code" "missing executable"
      return
    fi
    local logf="${REPORT_ARTIFACTS_DIR}/cases/${id}/stdout.txt"
    if [[ "$USE_CTEST" -eq 1 ]]; then
      if (cd "${BUILD_DIR}" && ctest -R "^${id}$" --output-on-failure -C "${CONFIG}") >"$logf" 2>&1; then
        echo "PASS: ${id}"
        record_case "$id" "pass" "mod" "exit_code" "ctest ok" "" "artifacts/cases/${id}/stdout.txt"
      else
        echo "FAIL: ${id}"
        SUITE_FAILURES=$((SUITE_FAILURES + 1))
        failures=$((failures + 1))
        record_case "$id" "fail" "mod" "exit_code" "ctest failed" "" "artifacts/cases/${id}/stdout.txt"
      fi
      return
    fi
    if "$path" >"$logf" 2>&1; then
      echo "PASS: ${id}"
      record_case "$id" "pass" "mod" "exit_code" "exit 0" "" "artifacts/cases/${id}/stdout.txt"
    else
      echo "FAIL: ${id} exited non-zero"
      SUITE_FAILURES=$((SUITE_FAILURES + 1))
      failures=$((failures + 1))
      record_case "$id" "fail" "mod" "exit_code" "non-zero exit" "" "artifacts/cases/${id}/stdout.txt"
    fi
    # show last lines of log for CI
    tail -n 5 "$logf" 2>/dev/null || true
  }
  run_mod_one mod_domain
  run_mod_one mod_network
  suite_finalize
  REPORT_PATHS+=("${REPORT_HTML}")
fi

# ---------------------------------------------------------------------------
# int
# ---------------------------------------------------------------------------
if [[ "$RUN_INT" -eq 1 ]]; then
  suite_begin int
  INT_CASES=(fake_overview fake_side_cam fake_selection_sobel)
  path="$(bin_path int_visual)"
  if [[ -z "$path" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: missing int_visual"
    SUITE_FAILURES=$((SUITE_FAILURES + 1))
    failures=$((failures + 1))
    record_case "int_visual" "fail" "int" "visual_golden" "missing int_visual"
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo ""
    echo "== VnV [int] int_visual =="
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    SUITE_FAILURES=$((SUITE_FAILURES + 1))
    failures=$((failures + 1))
    record_case "int_visual" "fail" "int" "visual_golden" "no display"
  else
    for case in "${INT_CASES[@]}"; do
      harness_out="vnv/int/tests/visual/out/${case}"
      actual_h="${harness_out}/actual.png"
      if [[ "$HEADLESS" == "1" || "$HEADLESS" == "true" ]]; then
        golden="vnv/int/tests/visual/goldens/linux_headless/${case}.png"
        compare_profile="headless"
      else
        golden="vnv/int/tests/visual/goldens/${case}.png"
        compare_profile="desktop"
      fi
      diff_h="${harness_out}/diff.png"
      report_h="${harness_out}/report.txt"
      echo ""
      echo "== VnV [int] int_visual (${case}) headless=${HEADLESS} golden=${golden} =="
      mkdir -p "$harness_out" "${REPORT_ARTIFACTS_DIR}/cases/${case}"
      rm -f "$actual_h"
      if ! "$path" --case "$case" --out "$actual_h" "${HEADLESS_ARGS[@]}"; then
        echo "FAIL: int_visual capture (${case})"
        SUITE_FAILURES=$((SUITE_FAILURES + 1))
        failures=$((failures + 1))
        record_case "$case" "fail" "int" "visual_golden" "capture failed"
      else
        rel_actual="$(vnv_report_stage_case_file "$case" "actual.png" "$actual_h")"
        rel_expected=""
        rel_diff=""
        rel_report=""
        if [[ -f "$golden" ]]; then
          rel_expected="$(vnv_report_stage_case_file "$case" "expected.png" "$golden")"
        fi
        if [[ "$UPDATE_GOLDENS" -eq 1 ]]; then
          mkdir -p "$(dirname "$golden")"
          cp -f "$actual_h" "$golden"
          echo "PASS: updated golden $golden"
          record_case "$case" "pass" "int" "visual_golden" "updated golden" "$rel_expected" "$rel_actual"
        elif [[ ! -f "$golden" ]]; then
          if python3 vnv/int/tests/visual/compare_images.py \
              --actual "$actual_h" --profile smoke --report-out "$report_h"; then
            echo "PASS: int_visual smoke (no golden)"
            rel_report="$(vnv_report_stage_case_file "$case" "report.txt" "$report_h")"
            record_case "$case" "pass" "int" "visual_golden" "smoke (no golden)" "" "$rel_actual" "" "$rel_report"
          else
            echo "FAIL: int_visual smoke"
            SUITE_FAILURES=$((SUITE_FAILURES + 1))
            failures=$((failures + 1))
            rel_report="$(vnv_report_stage_case_file "$case" "report.txt" "$report_h")"
            record_case "$case" "fail" "int" "visual_golden" "smoke failed" "" "$rel_actual" "" "$rel_report"
          fi
        elif python3 vnv/int/tests/visual/compare_images.py \
            --expected "$golden" --actual "$actual_h" \
            --profile "$compare_profile" \
            --diff-out "$diff_h" --report-out "$report_h"; then
          echo "PASS: int_visual ${case} (${compare_profile})"
          rel_diff="$(vnv_report_stage_case_file "$case" "diff.png" "$diff_h")"
          rel_report="$(vnv_report_stage_case_file "$case" "report.txt" "$report_h")"
          record_case "$case" "pass" "int" "visual_golden" "compare ok (${compare_profile})" \
            "$rel_expected" "$rel_actual" "$rel_diff" "$rel_report"
        else
          echo "FAIL: int_visual compare"
          SUITE_FAILURES=$((SUITE_FAILURES + 1))
          failures=$((failures + 1))
          [[ -f "$diff_h" ]] && rel_diff="$(vnv_report_stage_case_file "$case" "diff.png" "$diff_h")"
          rel_report="$(vnv_report_stage_case_file "$case" "report.txt" "$report_h")"
          record_case "$case" "fail" "int" "visual_golden" "compare failed" \
            "$rel_expected" "$rel_actual" "$rel_diff" "$rel_report"
        fi
      fi
    done
  fi
  suite_finalize
  REPORT_PATHS+=("${REPORT_HTML}")
fi

# ---------------------------------------------------------------------------
# bench
# ---------------------------------------------------------------------------
if [[ "$RUN_BENCH" -eq 1 ]]; then
  suite_begin bench
  BENCH_CASES=(fake_steady_frame)
  if [[ "$RUN_MASS" -eq 1 ]]; then
    BENCH_CASES+=(fake_mass_2k fake_mass_4k)
  fi
  if [[ -n "${BENCH_CASES_OVERRIDE:-}" ]]; then
    # shellcheck disable=SC2206
    BENCH_CASES=($BENCH_CASES_OVERRIDE)
  fi

  path="$(bin_path bench_frame_profiler)"
  if [[ -z "$path" ]]; then
    echo ""
    echo "== VnV [bench] =="
    echo "FAIL: missing bench_frame_profiler"
    SUITE_FAILURES=$((SUITE_FAILURES + 1))
    failures=$((failures + 1))
    record_case "bench_frame_profiler" "fail" "bench" "perf_baseline" "missing binary"
  elif [[ "$HEADLESS" != "1" && "$HEADLESS" != "true" && -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    echo ""
    echo "== VnV [bench] =="
    echo "FAIL: no DISPLAY/WAYLAND_DISPLAY — use --headless or set DISPLAY"
    SUITE_FAILURES=$((SUITE_FAILURES + 1))
    failures=$((failures + 1))
    record_case "bench_frame_profiler" "fail" "bench" "perf_baseline" "no display"
  else
    for case in "${BENCH_CASES[@]}"; do
      harness_out="vnv/bench/tests/out/${case}"
      actual_h="${harness_out}/actual.json"
      baseline="vnv/bench/baselines/${case}.json"
      echo ""
      echo "== VnV [bench] (${case}) headless=${HEADLESS} =="
      mkdir -p "$harness_out" "${REPORT_ARTIFACTS_DIR}/cases/${case}"
      if ! "$path" --case "$case" --out "$actual_h" "${HEADLESS_ARGS[@]}"; then
        echo "FAIL: bench capture (${case})"
        SUITE_FAILURES=$((SUITE_FAILURES + 1))
        failures=$((failures + 1))
        rel_actual=""
        metrics=""
        if [[ -f "$actual_h" ]]; then
          rel_actual="$(vnv_report_stage_case_file "$case" "actual.json" "$actual_h")"
          metrics="$(python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))))" "$actual_h" 2>/dev/null || true)"
        fi
        rel_base=""
        [[ -f "$baseline" ]] && rel_base="$(vnv_report_stage_case_file "$case" "baseline.json" "$baseline")"
        record_case "$case" "fail" "bench" "perf_baseline" "capture/confidence failed" \
          "$rel_base" "$rel_actual" "" "" "$metrics"
      else
        rel_actual="$(vnv_report_stage_case_file "$case" "actual.json" "$actual_h")"
        metrics="$(python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))))" "$actual_h")"
        if [[ "$UPDATE_BASELINES" -eq 1 ]]; then
          mkdir -p "$(dirname "$baseline")"
          cp -f "$actual_h" "$baseline"
          rel_base="$(vnv_report_stage_case_file "$case" "baseline.json" "$baseline")"
          echo "PASS: updated baseline $baseline"
          record_case "$case" "pass" "bench" "perf_baseline" "updated baseline" \
            "$rel_base" "$rel_actual" "" "" "$metrics"
        elif [[ ! -f "$baseline" ]]; then
          conf="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('confidence',1))" "$actual_h")"
          echo "PASS: bench ${case} (no baseline — confidence=${conf})"
          record_case "$case" "pass" "bench" "perf_baseline" "no baseline; soft confidence only" \
            "" "$rel_actual" "" "" "$metrics"
        else
          rel_base="$(vnv_report_stage_case_file "$case" "baseline.json" "$baseline")"
          if python3 - "$baseline" "$actual_h" "$BENCH_TOL" <<'PY'
import json, sys
base_p, act_p, tol = sys.argv[1], sys.argv[2], float(sys.argv[3])
base = json.load(open(base_p)); act = json.load(open(act_p))
ok = True
def check(label, b, a):
    global ok
    if b is None: return
    limit = float(b) * (1.0 + tol)
    if float(a) > limit:
        print(f"FAIL: {label} median {a:.3f} > baseline {b:.3f}*(1+{tol})={limit:.3f}")
        ok = False
    else:
        print(f"  ok {label}: actual={float(a):.3f} baseline={float(b):.3f}")
check("frame_ms", base.get("frame_ms", {}).get("median"), act.get("frame_ms", {}).get("median"))
check("cpu_ms", base.get("cpu_ms", {}).get("median"), act.get("cpu_ms", {}).get("median"))
check("gpu_ms", base.get("gpu_ms", {}).get("median"), act.get("gpu_ms", {}).get("median"))
if "total_blocks" in act and "total_blocks" in base:
    if float(act["total_blocks"]) < float(base["total_blocks"]) * 0.8:
        print(f"FAIL: total_blocks {act['total_blocks']} << baseline {base['total_blocks']}")
        ok = False
sys.exit(0 if ok else 1)
PY
          then
            echo "PASS: bench ${case}"
            record_case "$case" "pass" "bench" "perf_baseline" "baseline compare ok" \
              "$rel_base" "$rel_actual" "" "" "$metrics"
          else
            echo "FAIL: bench baseline compare (${case})"
            SUITE_FAILURES=$((SUITE_FAILURES + 1))
            failures=$((failures + 1))
            record_case "$case" "fail" "bench" "perf_baseline" "baseline compare failed" \
              "$rel_base" "$rel_actual" "" "" "$metrics"
          fi
        fi
      fi
    done
  fi
  suite_finalize
  REPORT_PATHS+=("${REPORT_HTML}")
fi

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "VnV: all selected suites passed"
else
  echo "VnV: ${failures} suite(s) failed"
fi
for p in "${REPORT_PATHS[@]+"${REPORT_PATHS[@]}"}"; do
  echo "  report: $p"
done
if [[ "$failures" -eq 0 ]]; then
  exit 0
fi
exit 1
