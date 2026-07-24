# VnV reports layout

Uniform report output for all Linux VnV runners. Mirrors test tiers: **mod**, **int**, **bench**.

```text
vnv/reports/
  README.md                          # this file (tracked)
  mod/<run_id>/                      # unit / module suite
  int/<run_id>/                      # system · functional (visual)
  bench/<run_id>/                    # system · bench (single-process cases)
  bench/matrix/<run_id>/             # multi-GPU matrix
  published/                         # optional committed snapshots for remote review
```

Each **run directory** contains:

| File | Role |
|------|------|
| `index.html` | Top HTML report (pass/fail, cases, expected/actual) |
| `run.json` | Machine-readable results (schema version 2) |
| `previous.json` | Prior suite `run.json` for before/after (optional) |
| `artifacts.zip` | Packaged inputs needed to regenerate `index.html` |
| `artifacts/` | Unpacked working artifacts |
| `matrix_table.html` | Compact GPU×case table (**matrix runs only**) |

## `run_id`

`YYYYMMDDTHHMMSSZ_<git_short>` (UTC).  
Also mirrored to `<suite>/latest/` after each run.

Override: `BLOCKVIZ_REPORT_RUN_ID=my_id` or `BLOCKVIZ_REPORTS_ROOT=other/path`.

## Artifact paths in `run.json`

All paths are **relative to the run directory**:

```json
{
  "version": 2,
  "suite": "bench",
  "run_id": "20260724T120000Z_abc1234",
  "status": "pass",
  "cases": [
    {
      "id": "fake_mass_2k",
      "status": "pass",
      "kind": "perf_baseline",
      "expected": "artifacts/cases/fake_mass_2k/baseline.json",
      "actual": "artifacts/cases/fake_mass_2k/actual.json"
    }
  ]
}
```

| Suite | Typical artifacts under `artifacts/cases/<id>/` |
|-------|--------------------------------------------------|
| mod | `stdout.txt` |
| int | `actual.png`, `expected.png`, `diff.png`, `report.txt` |
| bench | `actual.json`, `baseline.json` |
| bench/matrix | under `artifacts/cells/<gpu_tag>/<case>/` |

## Commands

```bash
# mod → vnv/reports/mod/<run_id>/
./scripts/run_vnv.sh --mod

# int headless → vnv/reports/int/<run_id>/
./scripts/run_vnv.sh --int --headless

# bench mass → vnv/reports/bench/<run_id>/
./scripts/run_vnv.sh --bench --mass --headless

# multi-GPU matrix → vnv/reports/bench/matrix/<run_id>/
xvfb-run -a ./scripts/run_bench_matrix.sh --no-headless --include-lavapipe --report

# regenerate HTML + zip from latest
./scripts/run_vnv.sh --report-only --suite mod
python3 scripts/generate_vnv_report.py --run-dir vnv/reports/bench/latest --zip
```

### Offline regenerate from zip

```bash
mkdir -p /tmp/vnv_run && unzip -o vnv/reports/mod/latest/artifacts.zip -d /tmp/vnv_run
python3 scripts/generate_vnv_report.py --run-dir /tmp/vnv_run --out /tmp/vnv_run/index.html
```

## Shared helpers

| Script | Role |
|--------|------|
| `scripts/vnv_report_layout.sh` | Begin run, stage files, update `latest` |
| `scripts/vnv_pack_report.py` | Build `artifacts.zip` |
| `scripts/generate_vnv_report.py` | HTML from `run.json` (`--run-dir`, optional `--zip`) |

Harness scratch outs may still appear under `vnv/*/tests/out/` (gitignored); **authoritative** report packages live only under `vnv/reports/`.
