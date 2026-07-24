# Published VnV snapshots (remote review)

Mirrors runtime layout under `vnv/reports/{mod,int,bench}/` (see [../README.md](../README.md)).

| Path | Contents |
|------|----------|
| [mod/latest/](mod/latest/) | Unit suite report + `artifacts.zip` |
| [int/](int/) | Visual captures (legacy snapshot; regenerate via `run_vnv.sh --int`) |
| [bench/](bench/) | Single-process bench actuals (legacy flat files) |
| [bench/matrix/](matrix/) → prefer regenerating into `vnv/reports/bench/matrix/` | Multi-GPU matrix |

**Hardware note:** multi-GPU farm uses PCIe ×1 risers; soft budgets are advisory.

```bash
./scripts/run_vnv.sh --mod
xvfb-run -a ./scripts/run_bench_matrix.sh --no-headless --include-lavapipe
# then copy latest → published/ for PR review if desired
```
