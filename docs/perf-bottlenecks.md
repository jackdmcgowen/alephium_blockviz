# Graphics performance bottlenecks

Living notes from IPass-scoped profiling (`FrameProfiler` / F3 HUD) and system · bench harnesses.

**How to measure**

```powershell
# F3 in product → frame table (scope names = IPass::name() + subscopes)
.\scripts\run_vnv.ps1 -Bench
# JSON: vnv/bench/tests/out/<case>/actual.json  (includes confidence 0–1)
```

**Pass scopes** (stable names): `MainColorDepth`, `Picker`, `SelectionDepth`, `SobelAsyncCMP`, `EdgeOverlay`, plus `Cubes`, `DebugMesh`, `ImGui`, host `Prepare` / `PublishUpload` / `RecordMain` / `SubmitPresent`.

## Current picture (typical discrete GPU, FakeChain)

| Scope | Typical role | Likely bottleneck when high | Actions |
|-------|--------------|----------------------------|---------|
| **MainColorDepth / Cubes** | Instanced block draw + MSAA | Instance count, fill, MSAA resolve | Cap visible instances; frustum cull; consider lower MSAA or deferred; LOD |
| **DebugMesh / MeshArenaUpload** | Arrows / planes upload + draw | CPU rebuild every frame | Dirty flags; cap edge/arrow count; GPU buffer reuse |
| **ImGui** | Overlay chrome | CPU layout + many widgets | Clip inactive windows; reduce HUD rate |
| **SelectionDepth + SobelAsyncCMP + EdgeOverlay** | Outline → CMP → composite | Multi-queue fence/semaphores; empty outline path still scheduled | Skip chain when `outline_count==0` (already); batch; avoid idle waits |
| **PublishUpload** | Triple-buffer instance copy | Large instance buffers | Keep latest-wins; avoid full memcpy when unchanged |
| **PresentSync** (bound class) | Frame wall ≫ work | Swapchain wait / vsync | Expected at 60 Hz; not a GPU algorithm issue |

## Soft budgets (bench confidence)

Harness soft budgets (ms median) — not absolute guarantees:

| Case | frame | cpu | gpu |
|------|-------|-----|-----|
| `fake_steady_frame` | 8 | 4 | 5 |
| `fake_stress_instances` | 12 | 6 | 8 |

Confidence \(C \in [0,1]\): weighted scores with **40%** soft band over budget.  
- \(C < 0.7\): warn  
- \(C < 0.4\): fail (serious regression)

Baselines under `vnv/bench/baselines/` remain for stricter device-local median checks via `run_vnv.ps1 -Bench`.

## Actionable backlog

1. **Instance / draw path** — profile `Cubes` under history scroll; add corridor cull if not present.  
2. **Debug drawer** — avoid per-frame full upload when arrows stable.  
3. **Sobel chain** — ensure zero-outline path never submits CMP (verify after outline changes).  
4. **Disk/network admit** — lazy disk admit (schedule 15, RAM admit ring) reduces hitch during boot/scroll.  
5. **Timeline chunks** — 60s history GETs (aligned with disk subsegments); camera-local network window + inflight backpressure limit endpoint pressure.

Update this table after major graphics changes with fresh bench JSON.
