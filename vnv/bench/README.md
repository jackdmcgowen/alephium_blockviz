# VnV · system · bench

**Tier:** **System · bench** — multi-system stack; **performance** pass/fail (not a separate product world).  
**Taxonomy:** [../TESTING.md](../TESTING.md).

> Sibling of functional system tests (`vnv/int/`). Same idea as shipping the product: engine + graphics (+ network as needed). Different criteria: **timing baselines**, not pixel goldens.

Performance regression using **`FrameTimingSnapshot`** (CPU scopes + GPU timestamps).

## Commands

```powershell
# Opt-in (not part of -All / free CI by default)
.\scripts\run_vnv.ps1 -Bench
.\scripts\run_vnv.ps1 -Bench -Configuration Release
.\scripts\run_vnv.ps1 -Bench -UpdateBaselines
```

## Standards

| Topic | Standard |
|-------|----------|
| Metrics | Median + p95 of `frame_ms`, `cpu_ms`, `gpu_ms`, and named scopes |
| Source | `IEngine::copy_frame_timing_snapshot` after `enable_frame_profiler(true)` |
| Config | Release\|x64 preferred for baselines; validation **off** in harness |
| Baseline | `vnv/bench/baselines/<id>.json` |
| Pass rule | Baseline compare: `median <= baseline * (1 + tol)` (tol **15%**); **and** harness soft-budget **confidence** ∈ [0,1] |
| Confidence | Weighted frame/cpu/gpu vs soft budgets; warn &lt; 0.7, fail &lt; 0.4 |
| Cases | `fake_steady_frame`, `fake_stress_instances`, dense overdraw/bfs, **mass** `fake_mass_2k` / `fake_mass_4k` |
| Update | `run_vnv.ps1 -Bench -UpdateBaselines` on a golden machine |
| Non-goals | Cross-machine absolute ms in free CI; replace `scripts/bench_build.ps1` |
| Bottlenecks | [docs/perf-bottlenecks.md](../../docs/perf-bottlenecks.md) |
| Reports | `./scripts/run_vnv.sh --bench --mass --headless --report` → `vnv/reports/last_run.html` |

## Layout

| Path | Role |
|------|------|
| `baselines/<case>.json` | Committed median/p95 budget |
| `tests/graphics_bench_tests.cpp` | Harness → `bench_frame_profiler` |
| `tests/out/<case>/actual.json` | Last run (gitignored) |
| `../reports/` | Run JSON + HTML (gitignored); see `scripts/generate_vnv_report.py` |
| `../manifest/case_catalog.json` | Case ids + descriptions for reports |

## Cases

| Case | Scene | Camera | Soft budgets (frame/cpu/gpu ms) |
|------|-------|--------|----------------------------------|
| `fake_steady_frame` | Default FakeChain (8×16) | Fixed | 8 / 4 / 5 |
| `fake_stress_instances` | Same as steady | Fixed | 12 / 6 / 8 |
| `fake_overdraw_end_z` | Dense bootstrap **48×16** | End, look **down +Z** | 28 / 26 / 2 |
| `fake_overdraw_end_z_move` | Dense 48×16 | End + **scroll Z** per sample | 30 / 28 / 2 |
| `fake_bfs_end_z` | Dense + **tip selection** | End down +Z | 30 / 28 / 3 |
| `fake_mass_2k` | Mass bootstrap **128×16 ≈ 2048** blocks | Fixed, pulled back | 50 / 48 / 4 |
| `fake_mass_4k` | Mass bootstrap **256×16 ≈ 4096** blocks | Fixed, pulled back | 90 / 88 / 6 |

Dense cases call `FakeChainSimulator::set_bootstrap_heights_override(48)`; mass uses 128 / 256 (default Debug stays 8). CLI override: `--bootstrap-heights N`.

Actual JSON also records `total_blocks`, `bootstrap_heights`, and `lane_count`.

- 1280×720, profiler on, validation off  
- Warmup ~2s steady / ~3.5s dense / ~6–8s mass; ~80–160 samples  
- Tracks frame/cpu/gpu + scopes (`Prepare`, `MainColorDepth`, `Cubes*`, …)

### Linux mass + HTML report

```bash
# Steady only (default --bench)
./scripts/run_vnv.sh --bench --headless --report

# Thousands of blocks + HTML with expected/actual + before/after
./scripts/run_vnv.sh --bench --mass --headless --report

# Or selective cases:
BENCH_CASES_OVERRIDE="fake_mass_2k fake_mass_4k" ./scripts/run_vnv.sh --bench --headless --report --skip-build
```

**Headless drivers:**

| Stack | Headless | Notes |
|-------|----------|--------|
| lavapipe (Mesa) | Yes | `VK_ICD_FILENAMES=.../lvp_icd.json` |
| NVIDIA proprietary | **No** | No `VK_EXT_headless_surface`; mixed Mesa surface **SEGVs**. Use **windowed** `DISPLAY` or `xvfb-run`. |

```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
```

Soft budgets are tuned for discrete GPUs; lavapipe may fail the confidence gate on
`fake_steady_frame` while mass cases (looser budgets + `total_blocks` scale) still pass.
Commit baselines from a representative GPU machine with `--update-baselines`.

### Multi-GPU matrix (this workstation has many cards)

```bash
# List Vulkan devices (and nvidia-smi indices)
./build/bin/bench_frame_profiler --list-devices --headless
./scripts/run_bench_matrix.sh --list-only

# Pick one GPU (needs DISPLAY/xvfb on NVIDIA — not headless)
CUDA_VISIBLE_DEVICES=2 NVIDIA_VISIBLE_DEVICES=2 \
  xvfb-run -a ./build/bin/bench_frame_profiler --case fake_steady_frame
./build/bin/bench_frame_profiler --device 2 --case fake_steady_frame   # with DISPLAY
./build/bin/bench_frame_profiler --device 3090 --case fake_mass_2k

# Full matrix: one process per GPU (+ optional lavapipe row)
./scripts/run_bench_matrix.sh --include-lavapipe --report --samples 40
# With real NVIDIA + virtual display:
xvfb-run -a ./scripts/run_bench_matrix.sh --no-headless \
  --cases fake_steady_frame,fake_mass_2k --parallel 4 --report

# Outputs: vnv/bench/tests/out/matrix/<tag>/<case>/actual.json
#          vnv/reports/matrix_run.json + matrix_run.html + matrix_table.html
```

Env: `BLOCKVIZ_DEVICE_INDEX` / `BLOCKVIZ_DEVICE_NAME` / `BLOCKVIZ_DEVICE_UUID`.
Actual JSON includes `device_name`, `device_index`, `device_uuid`, `device_type`.

How to add a case: [TESTING.md § System · bench](../TESTING.md#tier-c--system--bench-detail).
