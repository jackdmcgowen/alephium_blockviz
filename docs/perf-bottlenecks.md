# Graphics performance bottlenecks

Living notes from IPass-scoped profiling (`FrameProfiler` / F3 HUD) and system · bench harnesses.

**How to measure**

```powershell
# F3 in product → frame table (scope names = IPass::name() + subscopes)
.\scripts\run_vnv.ps1 -Bench
# JSON: vnv/bench/tests/out/<case>/actual.json  (includes confidence 0–1)
# Dense overdraw / prepare stress:
#   bench_frame_profiler --case fake_overdraw_end_z
```

**Pass scopes** (stable names): `MainColorDepth`, `Picker`, `SelectionDepth`, `SobelAsyncCMP`, `EdgeOverlay`, plus `InstanceCullCMP`, `CubesMesh` / `CubesClassic`, `DebugMesh`, `ImGui`, host `Prepare` / `PublishUpload` / `RecordMain` / `SubmitPresent`.

## Dense N truth (thousands of blocks)

Measured on discrete GPU, dense FakeChain (`fake_overdraw_end_z`, ~400–700 blocks in RAM):

| Scope | Order of magnitude | Role |
|-------|-------------------|------|
| **Prepare** | **~18–27 ms median** | Host layout + scene lock + ring filter + F2B sort |
| CubesMesh / MainColorDepth GPU | **≪ 0.1 ms** | Mesh + cull after 1.4.x |
| PublishUpload | ~0.01 ms | Instance memcpy |

**Framerate under heavy load is host-bound on the render thread**, not GPU fill/overdraw and not HTTP RTT. Network I/O is already async (`HttpIoPool`); the hitch is **admit → `graph_generation` → full layout rebuild inside `prepare`**, plus **shared `BlockScene` mutex** with the poller.

See [network.md § Interaction with graphics / frame rate](layers/network.md#interaction-with-graphics--frame-rate).

## Network → Prepare coupling

```text
poller: add_block / mark_confirmed  ──lock──►  graph_gen++
render: prepare()                   ──lock──►  if gen dirty: layout.build(ALL nodes)
                                               else: reuse layout_cache
                                               ring instances → publish (GPU cheap)
```

| Lever | Status |
|-------|--------|
| Render ring 7 G (draw only) | Done — does not cap layout cost |
| GPU frustum cull + mesh path | Done — GPU stays cheap |
| Opaque F2B sort | Done — small Prepare add-on for early-Z |
| Async HTTP + interval cap | Done |
| Snapshot under lock → unlock → layout | **P1** (plan) |
| Incremental / rate-limited layout | **P1** (plan) |
| Per-pump admit budget | **P2** (plan) |
| Secondary CBs per 64s subseg | **Rejected** — wrong layer for this bottleneck |

## Current picture (scope table)

| Scope | Typical role | Likely bottleneck when high | Actions |
|-------|--------------|----------------------------|---------|
| **Prepare** | Layout (full graph on gen change), filters, F2B sort | Continuous admits; large N; long mutex hold | Unlock snapshot; amortize layout; admit budget |
| **InstanceCullCMP** | GPU frustum compact | Rarely hot after prepare fixed | Cull on; later DrawMeshTasksIndirect |
| **CubesMesh / CubesClassic** | Cube draw | Only if Prepare fixed and fill-bound | F2B; optional opaque blend-off later |
| **DebugMesh / MeshArenaUpload** | Arrows / planes | CPU rebuild every frame | Dirty flags; cap arrows |
| **ImGui** | Overlay chrome | Many widgets | Clip inactive |
| **Picker** | Full pre-cull list, 1×1 | VS vs upload count | Keep pre-cull for pick_map IDs |
| **Sobel chain** | Outline → CMP → overlay | Tip count; fence | Skip if outline empty |
| **PublishUpload** | Instance copy | Large N memcpy | Latest-wins triple buffer |
| **PresentSync** | vsync wait | Expected at 60 Hz | Not an algorithm issue |

## Soft budgets (bench confidence)

Harness soft budgets (ms median) — not absolute guarantees:

| Case | frame | cpu | gpu |
|------|-------|-----|-----|
| `fake_steady_frame` | 8 | 4 | 5 |
| `fake_stress_instances` | 12 | 6 | 8 |
| `fake_overdraw_end_z` | 28 | 26 | 2 |
| `fake_overdraw_end_z_move` | 30 | 28 | 2 |
| `fake_bfs_end_z` | 30 | 28 | 3 |

Confidence \(C \in [0,1]\): weighted scores with **40%** soft band over budget.  
- \(C < 0.7\): warn  
- \(C < 0.4\): fail (serious regression)

Baselines under `vnv/bench/baselines/` remain for stricter device-local median checks via `run_vnv.ps1 -Bench`.

## Mesh + task (amplification) + F2B + prepare (landed)

| Decision | Rationale |
|----------|-----------|
| Task frustum (amplification default) | AABB cull in task; skip compute cull on that path |
| Mesh face/cone cull | Emit front faces only |
| Mesh-only + classic fallback | No task feature / no mesh |
| Opaque F2B sort (host, N≥256) | Early-Z |
| Prepare unlock + 50ms layout rate-limit | Snapshot under lock; rebuild off-lock |

**How to compare:** F3 `Prepare` vs `CubesTaskMesh` / `CubesMesh` on dense End view.

## Actionable backlog

1. **Incremental layout** — place only ΔN when possible.  
2. **Admit budget** — max blocks/ms per poll drain.  
3. **Opaque blend-off / dual draw** — if GPU shade becomes hot after Prepare is fixed.  
4. **Debug drawer** — avoid per-frame full upload when arrows stable.  
5. **Not planned:** secondary command buffers for network subsegments; depth prepass as first step.

Update this table after major host/network or graphics changes with fresh bench JSON.
