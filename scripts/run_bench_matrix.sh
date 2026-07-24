#!/usr/bin/env bash
# Multi-GPU bench matrix: one process per GPU (--device UUID isolation).
#
#   ./scripts/run_bench_matrix.sh
#   ./scripts/run_bench_matrix.sh --cases fake_steady_frame,fake_mass_2k
#   ./scripts/run_bench_matrix.sh --gpus 0,1,2 --parallel 3 --report
#   ./scripts/run_bench_matrix.sh --include-lavapipe --samples 20
#   ./scripts/run_bench_matrix.sh --list-only
#
# Reports (uniform layout):
#   vnv/reports/bench/matrix/<run_id>/
#     index.html  matrix_table.html  run.json  artifacts.zip
#     artifacts/cells/<gpu_tag>/<case>/actual.json
#
# Present: --headless (lavapipe); NVIDIA needs --no-headless + DISPLAY/xvfb-run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
# shellcheck source=vnv_report_layout.sh
source "${REPO_ROOT}/scripts/vnv_report_layout.sh"

BUILD_DIR="${BLOCKVIZ_BUILD_DIR:-build}"
CONFIG="Debug"
CASES="fake_steady_frame"
GPUS=""             # empty = all from nvidia-smi
PARALLEL=1
SAMPLES=""          # empty = harness defaults
WARMUP_MS=""
INCLUDE_LVP=0
REPORT=1            # always write report tree; --report kept for compat
SKIP_BUILD=0
HEADLESS=1
OUT_ROOT=""         # set after begin_run
RESULTS_JSON=""
RESULTS_HTML=""

usage() {
  sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cases) CASES="${2:-}"; shift 2 ;;
    --gpus) GPUS="${2:-}"; shift 2 ;;
    --parallel) PARALLEL="${2:-1}"; shift 2 ;;
    --samples) SAMPLES="${2:-}"; shift 2 ;;
    --warmup-ms) WARMUP_MS="${2:-}"; shift 2 ;;
    --include-lavapipe) INCLUDE_LVP=1; shift ;;
    --report) REPORT=1; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --no-headless) HEADLESS=0; shift ;;
    --out-root) OUT_ROOT="${2:-}"; shift 2 ;;
    --list-only) LIST_ONLY=1; shift ;;
    -h|--help) usage ;;
    *) echo "unknown arg: $1"; exit 2 ;;
  esac
done
LIST_ONLY="${LIST_ONLY:-0}"

bin_path() {
  local name="$1"
  if [[ -x "${BUILD_DIR}/bin/${name}" ]]; then echo "${BUILD_DIR}/bin/${name}"; return; fi
  if [[ -x "${BUILD_DIR}/bin/${CONFIG}/${name}" ]]; then echo "${BUILD_DIR}/bin/${CONFIG}/${name}"; return; fi
  echo ""
}

discover_gpus() {
  # Prints: index|name|uuid per line
  if ! command -v nvidia-smi >/dev/null 2>&1; then
    echo "WARN: nvidia-smi not found" >&2
    return
  fi
  nvidia-smi --query-gpu=index,name,uuid --format=csv,noheader,nounits 2>/dev/null \
    | sed 's/, /|/g' | sed 's/,$//' || true
}

if [[ "$SKIP_BUILD" -eq 0 && "$LIST_ONLY" -eq 0 ]]; then
  if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "No build dir; configure first or pass --skip-build"
    exit 2
  fi
  cmake --build "${BUILD_DIR}" --target bench_frame_profiler --config "${CONFIG}"
fi

BENCH="$(bin_path bench_frame_profiler)"
if [[ "$LIST_ONLY" -eq 0 && -z "$BENCH" ]]; then
  echo "FAIL: missing bench_frame_profiler"
  exit 1
fi

mapfile -t ALL_GPU_LINES < <(discover_gpus)
if [[ -n "$GPUS" ]]; then
  IFS=',' read -r -a WANT <<< "$GPUS"
  FILTERED=()
  for line in "${ALL_GPU_LINES[@]+"${ALL_GPU_LINES[@]}"}"; do
    [[ -z "$line" ]] && continue
    idx="${line%%|*}"
    for w in "${WANT[@]}"; do
      if [[ "$idx" == "$w" ]]; then
        FILTERED+=("$line")
        break
      fi
    done
  done
  ALL_GPU_LINES=("${FILTERED[@]+"${FILTERED[@]}"}")
fi

echo "== GPU discovery =="
if [[ ${#ALL_GPU_LINES[@]} -eq 0 || -z "${ALL_GPU_LINES[0]:-}" ]]; then
  echo "  (no NVIDIA GPUs from nvidia-smi)"
else
  for line in "${ALL_GPU_LINES[@]}"; do
    echo "  GPU $line"
  done
fi
if [[ "$INCLUDE_LVP" -eq 1 ]]; then
  echo "  + lavapipe (software ICD)"
fi

if [[ "$LIST_ONLY" -eq 1 ]]; then
  if [[ -n "$BENCH" ]]; then
    echo ""
    echo "== bench --list-devices (all visible) =="
    "$BENCH" --list-devices --headless 2>&1 || true
  fi
  exit 0
fi

IFS=',' read -r -a CASE_ARR <<< "$CASES"

# Uniform report run under bench/matrix
vnv_report_begin_run "bench/matrix"
OUT_ROOT="${REPORT_ARTIFACTS_DIR}/cells"
RESULTS_JSON="${REPORT_JSON}"
RESULTS_HTML="${REPORT_HTML}"
MATRIX_TABLE="${REPORT_RUN_DIR}/matrix_table.html"
mkdir -p "$OUT_ROOT"

# Build job list: tag|env_prefix|extra_args|case
JOBS_FILE="$(mktemp)"
trap 'rm -f "$JOBS_FILE"' EXIT

for line in "${ALL_GPU_LINES[@]+"${ALL_GPU_LINES[@]}"}"; do
  [[ -z "$line" ]] && continue
  idx="${line%%|*}"
  rest="${line#*|}"
  name="${rest%%|*}"
  uuid="${rest##*|}"
  # Sanitize tag
  tag="gpu${idx}_$(echo "$name" | tr ' /' '__' | tr -cd 'A-Za-z0-9._-')"
  for case in "${CASE_ARR[@]}"; do
    case="$(echo "$case" | xargs)"
    [[ -z "$case" ]] && continue
    out="${OUT_ROOT}/${tag}/${case}/actual.json"
    mkdir -p "$(dirname "$out")"
    extra=(--case "$case" --out "$out")
    [[ "$HEADLESS" -eq 1 ]] && extra+=(--headless)
    [[ -n "$SAMPLES" ]] && extra+=(--samples "$SAMPLES")
    [[ -n "$WARMUP_MS" ]] && extra+=(--warmup-ms "$WARMUP_MS")
    # env: isolate one GPU
    printf '%s\n' "${tag}|${idx}|${name}|${uuid}|${case}|${out}|CVD" >> "$JOBS_FILE"
  done
done

if [[ "$INCLUDE_LVP" -eq 1 ]]; then
  for case in "${CASE_ARR[@]}"; do
    case="$(echo "$case" | xargs)"
    [[ -z "$case" ]] && continue
    tag="lavapipe"
    out="${OUT_ROOT}/${tag}/${case}/actual.json"
    mkdir -p "$(dirname "$out")"
    printf '%s\n' "${tag}|-1|llvmpipe|lavapipe|${case}|${out}|LVP" >> "$JOBS_FILE"
  done
fi

run_one() {
  local tag="$1" idx="$2" name="$3" uuid="$4" case="$5" out="$6" mode="$7"
  local -a extra=(--case "$case" --out "$out")
  local use_headless="$HEADLESS"
  # NVIDIA + headless is unsupported — require windowed DISPLAY or skip.
  if [[ "$mode" != "LVP" && "$HEADLESS" -eq 1 ]]; then
    if [[ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
      echo ""
      echo "== [${tag}] case=${case} SKIP: NVIDIA headless unsupported without DISPLAY =="
      echo "   Install xvfb and: xvfb-run -a $0 --no-headless ...  (or set DISPLAY)"
      mkdir -p "$(dirname "$out")"
      python3 - "$out" "$tag" "$idx" "$name" "$uuid" <<'PY'
import json, sys
from pathlib import Path
out, tag, idx, name, uuid = sys.argv[1:6]
Path(out).parent.mkdir(parents=True, exist_ok=True)
doc = {
  "case": Path(out).parent.name,
  "status_note": "skipped_nvidia_headless",
  "matrix": {
    "matrix_tag": tag,
    "nvidia_index": int(idx) if str(idx).lstrip("-").isdigit() else idx,
    "nvidia_name": name,
    "nvidia_uuid": uuid,
    "exit_code": 0,
    "skipped": True,
    "reason": "NVIDIA has no VK_EXT_headless_surface; need DISPLAY/xvfb --no-headless",
  },
  "confidence": 1.0,
  "device_name": name,
  "device_index": int(idx) if str(idx).lstrip("-").isdigit() else -1,
  "device_uuid": uuid.replace("GPU-", "").replace("-", "").lower() if uuid else "",
  "frame_ms": {"median": None, "p95": None},
  "cpu_ms": {"median": None, "p95": None},
  "gpu_ms": {"median": None, "p95": None},
  "total_blocks": None,
}
Path(out).write_text(json.dumps(doc, indent=2) + "\n")
print("  wrote skip stub", out)
PY
      return 0
    fi
    # Have DISPLAY: force windowed for NVIDIA
    use_headless=0
    echo "  note: NVIDIA + DISPLAY → windowed (not headless)"
  fi
  [[ "$use_headless" -eq 1 ]] && extra+=(--headless)
  [[ -n "$SAMPLES" ]] && extra+=(--samples "$SAMPLES")
  [[ -n "$WARMUP_MS" ]] && extra+=(--warmup-ms "$WARMUP_MS")

  # Prefer --device UUID for Vulkan isolation (CUDA_VISIBLE_DEVICES does not
  # reliably mask NVIDIA Vulkan devices on multi-GPU Linux).
  local vk_uuid=""
  if [[ "$mode" != "LVP" && -n "$uuid" ]]; then
    vk_uuid="$(echo "$uuid" | sed 's/^GPU-//' | tr -d '-' | tr 'A-F' 'a-f')"
    extra+=(--device "$vk_uuid")
  fi

  echo ""
  echo "== [${tag}] case=${case} mode=${mode} headless=${use_headless} device=${vk_uuid:-auto} =="
  local rc=0
  if [[ "$mode" == "LVP" ]]; then
    env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
        VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
        CUDA_VISIBLE_DEVICES= \
        NVIDIA_VISIBLE_DEVICES= \
        "$BENCH" "${extra[@]}" || rc=$?
  else
    # Still set CVD as a secondary hint for CUDA/OpenCL side channels
    env CUDA_VISIBLE_DEVICES="$idx" NVIDIA_VISIBLE_DEVICES="$idx" \
        "$BENCH" "${extra[@]}" || rc=$?
  fi
  if [[ -f "$out" ]]; then
    echo "  wrote $out (rc=$rc)"
  else
    echo "  FAIL: no actual at $out (rc=$rc)"
  fi
  # stamp meta alongside
  python3 - "$out" "$tag" "$idx" "$name" "$uuid" "$rc" <<'PY' || true
import json, sys
from pathlib import Path
out, tag, idx, name, uuid, rc = sys.argv[1:7]
p = Path(out)
meta = {
    "matrix_tag": tag,
    "nvidia_index": int(idx) if str(idx).lstrip("-").isdigit() else idx,
    "nvidia_name": name,
    "nvidia_uuid": uuid,
    "exit_code": int(rc),
}
meta_path = p.parent / "matrix_meta.json"
if p.is_file():
    try:
        data = json.loads(p.read_text())
        data["matrix"] = meta
        p.write_text(json.dumps(data, indent=2) + "\n")
    except Exception as e:
        print("warn merge meta:", e)
meta_path.write_text(json.dumps(meta, indent=2) + "\n")
PY
  # Harness exit 3 = soft confidence only; metrics were written. Matrix
  # aggregation maps that to warn on discrete+healthy GPU (PCIe×1 / Xvfb).
  if [[ "$rc" -eq 3 && -f "$out" ]]; then
    return 0
  fi
  return $rc
}

export -f run_one
export BENCH HEADLESS SAMPLES WARMUP_MS

failures=0
if [[ "$PARALLEL" -le 1 ]]; then
  while IFS='|' read -r tag idx name uuid case out mode; do
    [[ -z "$tag" ]] && continue
    if ! run_one "$tag" "$idx" "$name" "$uuid" "$case" "$out" "$mode"; then
      failures=$((failures + 1))
    fi
  done < "$JOBS_FILE"
else
  # Parallel with job control
  running=0
  pids=()
  while IFS='|' read -r tag idx name uuid case out mode; do
    [[ -z "$tag" ]] && continue
    (
      run_one "$tag" "$idx" "$name" "$uuid" "$case" "$out" "$mode"
    ) &
    pids+=($!)
    running=$((running + 1))
    if [[ "$running" -ge "$PARALLEL" ]]; then
      if ! wait "${pids[0]}"; then failures=$((failures + 1)); fi
      pids=("${pids[@]:1}")
      running=$((running - 1))
    fi
  done < "$JOBS_FILE"
  for pid in "${pids[@]+"${pids[@]}"}"; do
    if ! wait "$pid"; then failures=$((failures + 1)); fi
  done
fi

# Aggregate JSON + HTML (always)
python3 - "$OUT_ROOT" "$RESULTS_JSON" "$failures" "$REPORT_RUN_ID" <<'PY'
import json, sys
from pathlib import Path
from datetime import datetime, timezone

out_root = Path(sys.argv[1])
results_path = Path(sys.argv[2])
failures = int(sys.argv[3])
run_id = sys.argv[4] if len(sys.argv) > 4 else ""

cases = []
run_dir = results_path.parent
for actual in sorted(out_root.glob("*/*/actual.json")):
    tag = actual.parent.parent.name
    case_id = actual.parent.name
    # path relative to run dir
    try:
        rel_actual = actual.resolve().relative_to(run_dir.resolve()).as_posix()
    except ValueError:
        rel_actual = str(actual)
    try:
        metrics = json.loads(actual.read_text())
    except Exception as e:
        cases.append({
            "id": f"{tag}/{case_id}",
            "status": "fail",
            "suite": "bench",
            "kind": "perf_baseline",
            "message": f"bad json: {e}",
            "actual": rel_actual,
            "matrix_tag": tag,
        })
        continue
    conf = metrics.get("confidence")
    mx = metrics.get("matrix") or {}
    status = "pass"
    msg = f"blocks={metrics.get('total_blocks')} conf={conf} device={metrics.get('device_name')}"
    dtype = (metrics.get("device_type") or "").lower()
    gpu_med = None
    try:
        gpu_med = float((metrics.get("gpu_ms") or {}).get("median") or 0)
    except (TypeError, ValueError):
        pass
    if mx.get("skipped") or metrics.get("status_note") == "skipped_nvidia_headless":
        status = "warn"
        msg = mx.get("reason") or "skipped (NVIDIA headless)"
    elif conf is not None and conf != "" and float(conf) < 0.4:
        if dtype in ("discrete", "integrated") and gpu_med is not None and gpu_med < 5.0:
            status = "warn"
            msg += " (soft conf low; PCIe×1/Xvfb advisory — GPU median healthy)"
        else:
            status = "fail"
            msg += " (confidence soft-fail)"
    elif mx.get("exit_code") not in (None, 0) and status == "pass":
        if int(mx.get("exit_code", 0)) not in (0, 3):
            status = "fail"
            msg += f" exit={mx.get('exit_code')}"
    if not mx.get("skipped") and metrics.get("samples") in (0, None) and metrics.get("frame_ms") is None:
        if metrics.get("status_note") != "skipped_nvidia_headless":
            status = "fail"
            msg = msg or "no samples"
    cases.append({
        "id": f"{tag}/{case_id}",
        "status": status,
        "suite": "bench",
        "kind": "perf_baseline",
        "message": msg,
        "expected": None,
        "actual": rel_actual,
        "matrix_tag": tag,
        "case": case_id,
        "metrics": metrics,
        "device_name": metrics.get("device_name"),
        "device_uuid": metrics.get("device_uuid"),
        "device_index": metrics.get("device_index"),
    })

n_fail = sum(1 for c in cases if c["status"] == "fail")
n_warn = sum(1 for c in cases if c["status"] == "warn")
if n_fail or failures:
    overall = "fail"
elif n_warn:
    overall = "warn"
else:
    overall = "pass"
doc = {
    "version": 2,
    "suite": "bench/matrix",
    "run_id": run_id,
    "status": overall,
    "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "kind": "bench_matrix",
    "hardware_note": "PCIe x1 risers; soft budgets advisory (relative multi-GPU farm)",
    "out_root": "artifacts/cells",
    "suites": ["bench"],
    "cases": cases,
    "matrix_summary": {
        "jobs": len(cases),
        "failed_cases": n_fail,
        "warn_cases": n_warn,
        "script_failures": failures,
    },
}
results_path.parent.mkdir(parents=True, exist_ok=True)
results_path.write_text(json.dumps(doc, indent=2) + "\n")
print(f"Wrote {results_path} ({len(cases)} cells, status={doc['status']})")
PY

# Always emit HTML + matrix table + zip
python3 scripts/generate_vnv_report.py \
  --run-dir "$REPORT_RUN_DIR" \
  --results "$RESULTS_JSON" \
  --catalog vnv/manifest/case_catalog.json \
  --out "$RESULTS_HTML"
python3 - "$RESULTS_JSON" "$MATRIX_TABLE" <<'PY'
import json, sys, html
from pathlib import Path
doc = json.loads(Path(sys.argv[1]).read_text())
out = Path(sys.argv[2])
# pivot: tag x case
cells = {}
tags = set()
cases = set()
for c in doc.get("cases") or []:
    tag = c.get("matrix_tag") or c["id"].split("/")[0]
    case = c.get("case") or c["id"].split("/")[-1]
    tags.add(tag)
    cases.add(case)
    m = c.get("metrics") or {}
    cells[(tag, case)] = {
        "status": c.get("status"),
        "frame": (m.get("frame_ms") or {}).get("median"),
        "cpu": (m.get("cpu_ms") or {}).get("median"),
        "blocks": m.get("total_blocks"),
        "device": m.get("device_name") or c.get("device_name"),
        "conf": m.get("confidence"),
    }
tags = sorted(tags)
cases = sorted(cases)
rows = []
for tag in tags:
    tds = [f"<td><code>{html.escape(tag)}</code></td>"]
    dev = ""
    for case in cases:
        cell = cells.get((tag, case))
        if not cell:
            tds.append("<td class='miss'>—</td>")
            continue
        if cell.get("device"):
            dev = cell["device"]
        st = cell["status"] or "?"
        cls = "pass" if st == "pass" else ("warn" if st == "warn" else "fail")
        fr = cell["frame"]
        fr_s = f"{fr:.2f}" if isinstance(fr, (int, float)) else "—"
        bl = cell["blocks"] if cell["blocks"] is not None else "—"
        tds.append(
            f"<td class='{cls}'><b>{html.escape(st)}</b><br/>"
            f"frame {html.escape(fr_s)} ms<br/>blocks {html.escape(str(bl))}</td>"
        )
    rows.append(f"<tr><td class='dev'>{html.escape(dev)}</td>" + "".join(tds) + "</tr>")
head = "".join(f"<th>{html.escape(c)}</th>" for c in cases)
body = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"/><title>Bench matrix</title>
<style>
body {{ font-family: system-ui, sans-serif; background:#0f1419; color:#e7ecf3; padding:1.5rem; }}
table {{ border-collapse: collapse; width:100%; font-size:0.9rem; }}
th, td {{ border:1px solid #2a3548; padding:0.5rem; text-align:left; vertical-align:top; }}
th {{ background:#121a26; }}
.pass {{ background: rgba(61,214,140,0.12); }}
.fail {{ background: rgba(240,113,120,0.15); }}
.warn {{ background: rgba(230,180,80,0.12); }}
.miss {{ color:#8b9bb4; }}
.dev {{ color:#8b9bb4; font-size:0.8rem; max-width:12rem; }}
h1 {{ font-size:1.3rem; }}
.note {{ color:#8b9bb4; max-width:52rem; line-height:1.45; }}
</style></head><body>
<h1>Bench matrix — {html.escape(doc.get('status','?').upper())}</h1>
<p>{html.escape(doc.get('generated_at',''))} · jobs={doc.get('matrix_summary',{}).get('jobs')}</p>
<p class="note">Hardware note: multi-GPU farm on <strong>PCIe ×1 risers</strong> (host↔device bandwidth limited).
Soft budgets are desktop ×16-oriented — treat frame/conf as <em>advisory</em>; use GPU median + total_blocks
and relative ranking across cards. Isolation uses Vulkan <code>--device</code> UUID (not CUDA_VISIBLE_DEVICES alone).</p>
<table>
<thead><tr><th>Device</th><th>Tag</th>{head}</tr></thead>
<tbody>
{''.join(rows)}
</tbody></table>
<p><a href="index.html" style="color:#7eb8ff">Full report</a></p>
</body></html>
"""
out.write_text(body)
print(f"Wrote {out}")
PY
vnv_report_finalize
echo "Matrix report: $RESULTS_HTML"
echo "Matrix table:  $MATRIX_TABLE"
echo "Matrix zip:    $REPORT_ZIP"

echo ""
if [[ "$failures" -eq 0 ]]; then
  echo "Matrix: finished (see $RESULTS_JSON)"
  exit 0
fi
echo "Matrix: ${failures} job(s) failed"
exit 1
