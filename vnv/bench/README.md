# VnV · bench (deferred)

Performance regression category — **not implemented in V0**.

## Future standards

| Topic | Standard |
|-------|----------|
| Metrics | Median + p95 frame time (ms); optional FakeChain CPU step; build times via `scripts/bench_build.ps1` |
| Config | Release\|x64 preferred; validation **off** for render benches |
| Baseline | `vnv/bench/baselines/<id>.json` |
| Pass rule | `median <= baseline * (1 + tol)` with tol **10–15%** |
| Update | `run_vnv.ps1 -Bench -UpdateBaselines` on golden machine |
| Non-goals | Cross-machine absolute ms; free CI GPU |

When implemented, place harness sources under `vnv/bench/tests/` and register in `vnv/manifest/vnv_projects.json` with `"category": "bench"`.
