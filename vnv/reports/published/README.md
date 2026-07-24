# Published VnV run (Linux)

Snapshot from branch `feature/linux-testing-reports` on the Linux workstation.

| Artifact | Description |
|----------|-------------|
| [last_run.html](last_run.html) | Full HTML report (pass/fail, metrics, before/after) |
| [last_run.json](last_run.json) | Machine-readable results |
| [previous_run.json](previous_run.json) | Prior run for before/after |
| `bench/*.actual.json` | Bench actuals (steady, mass_2k ~2061 blocks, mass_4k ~4113 blocks) |
| `int/fake_overview/` | Headless visual capture (actual PNG) |

**Driver:** lavapipe (llvmpipe) via `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`  
**Note:** Soft budgets are discrete-GPU tuned; `fake_steady_frame` may soft-fail confidence on software raster while mass cases pass.

Regenerate locally:
```bash
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
./scripts/run_vnv.sh --bench --mass --headless --report
```

## Multi-GPU matrix

See [matrix/](matrix/) — discovered **7× NVIDIA** + lavapipe.

| Reality | Detail |
|---------|--------|
| NVIDIA proprietary | No `VK_EXT_headless_surface` — matrix marks rows **warn/skipped** without DISPLAY/xvfb |
| lavapipe | Headless runs; soft budgets may confidence-fail on software |
| Unlock real multi-GPU | `sudo apt install xvfb` then `xvfb-run -a ./scripts/run_bench_matrix.sh --no-headless --report` |

```bash
./build/bin/bench_frame_profiler --list-devices --headless
./scripts/run_bench_matrix.sh --list-only
```
