#!/usr/bin/env bash
# Shared VnV report paths and run-dir lifecycle.
# Source from repo root:  source scripts/vnv_report_layout.sh
#
# Layout:
#   vnv/reports/{mod,int,bench}/<run_id>/
#   vnv/reports/bench/matrix/<run_id>/
# Each run dir:
#   index.html  run.json  previous.json  artifacts.zip  artifacts/
#
# Env:
#   BLOCKVIZ_REPORTS_ROOT  default vnv/reports
#   BLOCKVIZ_REPORT_RUN_ID optional fixed run id (tests)

# shellcheck disable=SC2034
REPORTS_ROOT="${BLOCKVIZ_REPORTS_ROOT:-vnv/reports}"

vnv_report_git_short() {
  git rev-parse --short HEAD 2>/dev/null || echo "nogit"
}

vnv_report_new_run_id() {
  if [[ -n "${BLOCKVIZ_REPORT_RUN_ID:-}" ]]; then
    echo "${BLOCKVIZ_REPORT_RUN_ID}"
    return
  fi
  # UTC compact: 20260724T033000Z_e82a270
  local ts
  ts="$(date -u +"%Y%m%dT%H%M%SZ")"
  echo "${ts}_$(vnv_report_git_short)"
}

# suite path under REPORTS_ROOT: mod | int | bench | bench/matrix
vnv_report_suite_dir() {
  local suite="$1"
  echo "${REPORTS_ROOT}/${suite}"
}

# Begin a run. Sets globals:
#   REPORT_SUITE REPORT_RUN_ID REPORT_RUN_DIR REPORT_ARTIFACTS_DIR
#   REPORT_JSON REPORT_HTML REPORT_PREV REPORT_ZIP
# Usage: vnv_report_begin_run mod
#        vnv_report_begin_run bench/matrix
vnv_report_begin_run() {
  local suite="$1"
  REPORT_SUITE="$suite"
  REPORT_RUN_ID="$(vnv_report_new_run_id)"
  REPORT_RUN_DIR="$(vnv_report_suite_dir "$suite")/${REPORT_RUN_ID}"
  REPORT_ARTIFACTS_DIR="${REPORT_RUN_DIR}/artifacts"
  REPORT_JSON="${REPORT_RUN_DIR}/run.json"
  REPORT_HTML="${REPORT_RUN_DIR}/index.html"
  REPORT_PREV="${REPORT_RUN_DIR}/previous.json"
  REPORT_ZIP="${REPORT_RUN_DIR}/artifacts.zip"

  mkdir -p "${REPORT_ARTIFACTS_DIR}/cases"
  # Carry forward previous latest for before/after
  local latest_json
  latest_json="$(vnv_report_suite_dir "$suite")/latest/run.json"
  if [[ -f "$latest_json" ]]; then
    cp -f "$latest_json" "${REPORT_PREV}"
  fi
  echo "report run: ${REPORT_RUN_DIR}"
}

# Copy/link harness file into artifacts/cases/<case_id>/<name>
# Usage: vnv_report_stage_case_file fake_overview actual.png path/to/actual.png
vnv_report_stage_case_file() {
  local case_id="$1" dest_name="$2" src="$3"
  local dest_dir="${REPORT_ARTIFACTS_DIR}/cases/${case_id}"
  mkdir -p "$dest_dir"
  if [[ -f "$src" ]]; then
    cp -f "$src" "${dest_dir}/${dest_name}"
    # relative path from run dir
    echo "artifacts/cases/${case_id}/${dest_name}"
  else
    echo ""
  fi
}

# Relative path helpers (from run dir)
vnv_report_rel_actual() {
  local case_id="$1" name="${2:-actual.json}"
  echo "artifacts/cases/${case_id}/${name}"
}

# Point suite/latest at this run (copy tree for portability; no symlink required)
vnv_report_update_latest() {
  local suite="${1:-$REPORT_SUITE}"
  local run_dir="${2:-$REPORT_RUN_DIR}"
  local suite_dir
  suite_dir="$(vnv_report_suite_dir "$suite")"
  local latest="${suite_dir}/latest"
  rm -rf "$latest"
  mkdir -p "$latest"
  # Copy essential + artifacts (not nested latest)
  if [[ -d "$run_dir" ]]; then
    cp -a "$run_dir"/. "$latest"/
  fi
  # Pointer file for humans
  printf '%s\n' "$(basename "$run_dir")" > "${suite_dir}/LATEST_RUN_ID"
  echo "report latest: ${latest}"
}

# Finalize: HTML already written; pack zip; update latest
# Usage: vnv_report_finalize
vnv_report_finalize() {
  if [[ -z "${REPORT_RUN_DIR:-}" ]]; then
    echo "vnv_report_finalize: no active run" >&2
    return 1
  fi
  if command -v python3 >/dev/null 2>&1; then
    python3 "$(dirname "${BASH_SOURCE[0]}")/vnv_pack_report.py" --run-dir "${REPORT_RUN_DIR}" || true
  fi
  vnv_report_update_latest
}
