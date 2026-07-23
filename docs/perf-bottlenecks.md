# Graphics performance bottlenecks

Living notes from IPass-scoped profiling (`FrameProfiler` / F3 HUD) and system · bench harnesses.

**How to measure**

```powershell
# F3 in product → frame table (scope names = IPass::name() + subscopes)
.\scripts\run_vnv.ps1 -Bench
# JSON: vnv/bench/tests/out/<case>/actual.json  (includes confidence 0–1)
```

**Pass scopes** (stable names): `MainColorDepth`, `Picker`, `SelectionDepth`, `SobelAsyncCMP`, `EdgeOverlay`, plus `InstanceCullCMP`, `CubesMesh` / `CubesClassic`, `DebugMesh`, `ImGui`, host `Prepare` / `PublishUpload` / `RecordMain` / `SubmitPresent`.

## Current picture (typical discrete GPU, FakeChain)

| Scope | Typical role | Likely bottleneck when high | Actions |
|-------|--------------|----------------------------|---------|
| **InstanceCullCMP** | GPU frustum compact of cube instances | Huge host upload still uploaded; compute occupancy | Already view-culls; keep presenter ring/product filters first |
| **MainColorDepth / CubesMesh** | Mesh-shader cubes (when `VK_EXT_mesh_shader` on) | Task/mesh WG count ≈ host instance count (empty WGs if culled) | Prefer cull on; later: `DrawMeshTasksIndirect` |
| **MainColorDepth / CubesClassic** | Instanced VBO/IBO (+ indirect after cull) | Instance count, fill, MSAA resolve | Cull reduces `instanceCount`; lower MSAA / LOD if needed |
| **DebugMesh / MeshArenaUpload** | Arrows / planes upload + draw | CPU rebuild every frame | Dirty flags; cap edge/arrow count; GPU buffer reuse |
| **ImGui** | Overlay chrome | CPU layout + many widgets | Clip inactive windows; reduce HUD rate |
| **Picker** | 1×1 id buffer over **full pre-cull** list | VS cost scales with upload count | Kept pre-cull for `pick_map` IDs; compact would need original-index field |
| **SelectionDepth + SobelAsyncCMP + EdgeOverlay** | Outline → CMP → composite | Multi-queue fence; tip count | Skip when `outline_count==0`; PR4 CPU frustum drops off-screen tips |
| **Prepare** | Host layout + filters + **opaque F2B sort** | Dense visible N log N sort | Sort is intentional for early-Z; keep ring filters first |
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

## Mesh + GPU cull + F2B sort

| Decision | Rationale |
|----------|-----------|
| Main cubes use GPU frustum compact | Cuts shaded cubes under tight look; classic indirect or mesh clamp |
| Mesh path default when extension enabled | Same `frag.spv`; classic remains fallback (`prefer_mesh_cube`) |
| Picker **not** on compact SSBO | `gl_InstanceIndex` must match `pick_map` without original-index payload |
| Outline CPU frustum at upload | Tiny list; skip off-screen tips before Sobel chain |
| **Opaque front-to-back sort** (host) | `alpha ≥ 0.99` near→far before upload; translucent after; improves early-Z without depth prepass |

**How to compare paths:** F3 → `CubesMesh` / `CubesClassic` / `InstanceCullCMP` while looking **End** (down +Z) on a dense ring. Bench cases: `fake_overdraw_end_z`, `fake_overdraw_end_z_move`, `fake_bfs_end_z`.

## Actionable backlog

1. **Mesh dispatch** — `DrawMeshTasksIndirectEXT` from cull count (drop empty mesh WGs).  
2. **Opaque blend-off / dual draw** — if F2B alone is not enough under dense end-Z benches.  
3. **Picker scale** — optional original-index in cull output if full-list pick VS becomes hot.  
4. **Debug drawer** — avoid per-frame full upload when arrows stable.  
5. **Sobel chain** — ensure zero-outline path never submits CMP (verify after outline changes).  
6. **Disk/network admit** — lazy disk admit (schedule 15, RAM admit ring) reduces hitch during boot/scroll.

Update this table after major graphics changes with fresh bench JSON.
