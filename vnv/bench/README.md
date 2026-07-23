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
| Cases | `fake_steady_frame`, `fake_stress_instances`, `fake_overdraw_end_z`, `fake_overdraw_end_z_move`, `fake_bfs_end_z` |
| Update | `run_vnv.ps1 -Bench -UpdateBaselines` on a golden machine |
| Non-goals | Cross-machine absolute ms in free CI; replace `scripts/bench_build.ps1` |
| Bottlenecks | [docs/perf-bottlenecks.md](../../docs/perf-bottlenecks.md) |

## Layout

| Path | Role |
|------|------|
| `baselines/<case>.json` | Committed median/p95 budget |
| `tests/graphics_bench_tests.cpp` | Harness → `bench_frame_profiler.exe` |
| `tests/out/<case>/actual.json` | Last run (gitignored) |

## Cases

| Case | Scene | Camera | Soft budgets (frame/cpu/gpu ms) |
|------|-------|--------|----------------------------------|
| `fake_steady_frame` | Default FakeChain (8×16) | Fixed | 8 / 4 / 5 |
| `fake_stress_instances` | Same as steady | Fixed | 12 / 6 / 8 |
| `fake_overdraw_end_z` | Dense bootstrap **48×16** | End, look **down +Z** | 28 / 26 / 2 |
| `fake_overdraw_end_z_move` | Dense 48×16 | End + **scroll Z** per sample | 30 / 28 / 2 |
| `fake_bfs_end_z` | Dense + **tip selection** | End down +Z | 30 / 28 / 3 |

Dense cases call `FakeChainSimulator::set_bootstrap_heights_override(48)` before start (default Debug stays 8).

- 1280×720, profiler on, validation off  
- Warmup ~2s steady / ~3.5s dense; ~120–160 samples  
- Tracks frame/cpu/gpu + scopes (`Prepare`, `MainColorDepth`, `Cubes*`, …)

How to add a case: [TESTING.md § System · bench](../TESTING.md#tier-c--system--bench-detail).
