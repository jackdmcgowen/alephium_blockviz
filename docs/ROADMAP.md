# Roadmap / priority backlog

Ordered “what else” for **alephium_blockviz**. Living layer goals: [layers/README.md](layers/README.md).  
Historical designs are archives, not the backlog: [modularization](graphics-modularization-design.md), [confirmed tips](blockflow-confirmed-tips-design.md).  
Platform / Linux layout: [platform.md](platform.md) · [linux.md](linux.md).

**Last updated:** 2026-07-23 (mesh/cull graphics path shipped 1.4.0)  
**Versions:** app **1.4.1** · engine **1.4.1** (see identity headers / tags on `main`)

| Status | Meaning |
|--------|---------|
| **Done** | Shipped on `main` or equivalent; keep for context |
| **P0** | Do next (recommended order) |
| **P1** | Soon after P0 |
| **P2** | Later / optional |
| **Out** | Explicit non-goal — do not schedule |

---

## Done (context)

| Item | Notes |
|------|--------|
| Modular host / domain / network / engine / graphics | See layer docs |
| Confirmed tips + evolved frontier (`H_c`, walks, cyan/orange) | Network marks · app presents · graphics Sobel |
| Detail store slim + selection refill (PR11) | [network](layers/network.md) |
| Build PCH / `/MP` / incremental shaders | [build-performance](build-performance.md) |
| Living layer docs (app, engine, graphics, network, domain) | [layers/](layers/README.md) |
| Offline **Debug / FakeChain** simulator | [network](layers/network.md) |
| Domain / network **mod** unit tests | `vnv/mod/tests/` · CMake + MSVC |
| Config persistence + prune policy | `user_prefs.json` · `BlockScene::prune` |
| **Segment disk cache** (gzip segments, bootstrap) | [segment-disk-cache.md](segment-disk-cache.md) · shipped 1.0 |
| Frame profiler + bench harness | F3 HUD · `bench_frame_profiler` |
| **Platform seams** (`app`/`graphics`/`network` `*_win32` / `*_linux`) | [platform.md](platform.md) |
| **Linux CMake + GLFW product build** | [linux.md](linux.md) · `CMakeLists.txt` |
| **Headless present** (`VK_EXT_headless_surface`) + GPU PNG readback | `--headless` harnesses |
| **Linux VnV runner** + GitHub Actions mod CI | `scripts/run_vnv.sh` · `.github/workflows/linux-ci.yml` |
| Graphics visual / bench harnesses (portable host) | `int_visual` · `bench_frame_profiler` |
| **VnV framework** (unit/system/bench + dual slns) | `vnv/` · [TESTING.md](../vnv/TESTING.md) · `run_vnv.ps1` |
| **PCH include audit** (`scripts/check_pch.py`) | CI + AGENTS |
| **App include-boundary audit** (`scripts/check_include_boundary.py`) | App must not include Vulkan / `gpu_platform.hpp` |
| CRT helpers (`common/time_util.hpp`, `env_util.hpp`) | Portable local_time / env flags |
| **GPU screenshot primary**; window blit opt-in | `BLOCKVIZ_SCREENSHOT_WINDOW_BLIT=1` |
| **Multi-platform goldens** + headless CI hard gate | `goldens/linux_headless/` · `compare_images.py --profile` |
| **Dual-track smoke** script | `scripts/smoke_dual_track.sh` |
| **Linux platform on `main`** (1.1.0) | CMake product, headless, GHA mod CI, dual-track docs |
| **IPass frame graph** (pipelines as nodes) | `frame/passes/*` + `SamplerTable`; F12 pre-present GPU readback |
| **Int visual cases** | `fake_overview`, `fake_side_cam`, `fake_selection_sobel` |
| **64s / 640s timeline grid** + fill volumes + live open cell | [network](layers/network.md) · [user guide](index.html) |
| **Camera-subseg history interval walk** (eye → next unfilled) | Adapter `pump_history_from_camera_` |
| **Domain-agnostic single-pass Sobel** | App colors × white edge |
| **GPU frustum cull + classic indirect** | `InstanceCullPass` · 1.4.0 |
| **Mesh cube path (`VK_EXT_mesh_shader`)** | `cube.mesh.glsl` · classic fallback · 1.4.0 |
| **Outline CPU frustum + pick/cull policy** | Picker pre-cull; outline filtered at upload · 1.4.0 |

---

## P0 — Next (recommended order)

| # | Item | Layer | Why |
|---|------|-------|-----|
| 1 | **Vulkan-free host hooks header** | graphics · app | Split `configure_headless` (etc.) out of `gpu_platform.hpp` — app can include without Vulkan |

---

## P1 — Soon

| # | Item | Layer | Why |
|---|------|-------|-----|
| 3 | Richer **dep-viz modes** (LOD / filters) | [app](layers/app.md) | Selection BFS shipped; avoid edge soup |
| 4 | **Confirm polish** (feed badges, green vs shard eye-check) | [app](layers/app.md) | Post-MVP from confirmed-tips design |
| 5 | **WebSocket** tip stream | [network](layers/network.md) | Lower latency; focused feature |
| 6 | **DrawMeshTasksIndirect** from cull count | graphics | Drop empty mesh workgroups after GPU cull |

---

## P2 — Later

| # | Item | Layer | Why |
|---|------|-------|-----|
| 7 | History presentation LOD | app · network | Sliding ring in RAM; presentation LOD open |
| 8 | Second real chain adapter | [network](layers/network.md) | After FakeChain multi-adapter wiring |
| 9 | CMake-primary on Windows (keep sln optional) | build | One target graph |
| 10 | Unify Windows host on GLFW | app · graphics | Drop dual host implementations |
| 11 | AppImage / packaging | ship | Optional |
| 12 | macOS / MoltenVK | platform | New `*_macos.cpp` — unscheduled |
| 13 | Graph-driven frame record (full IPass executor) | graphics | Topology is registered; multi-queue still in `SobelAsyncPass` |
| 14 | Physical VnV path rename (`mod`→`unit`, `int`→`system/functional`, `bench`→`system/bench`) | vnv | Docs already use taxonomy in [vnv/TESTING.md](../vnv/TESTING.md); migrate when convenient |

---

## Out of scope (do not schedule)

| Item | Reason |
|------|--------|
| Dual exclusive **selection-gold vs tip-green modes** fighting for one overlay | Gold wins when selected; multi-role multi-color outlines in one pass are intentional |
| Green cube **body fill** / full main-chain paint | Outline + arrows product |
| D3D / OpenGL / WebGPU rewrite | Stack non-goal |
| Full ECS, plugins, scripting | Overkill |
| Structured logging frameworks | printf + ImGui OK |
| Multi-chain **marketplace** UI | Product non-goal |
| Production auth / retry / multi-endpoint HA platform | REST poll is enough for v1 |
| DRM display-plane present as CI path | Headless surface is enough |

---

## Portability rule of thumb

```text
Touches OS / WSI / paths / process / CRT?
  → src/<layer>/platform/* or src/common/*
Else
  → domain / engine / network policy / app presenter

New MSVC product .cpp (PCH=Use)?
  → first line: #include "<area>/pch.h"   (includes platform/*_win32.cpp)
  → python scripts/check_pch.py

App code needing a graphics platform hook?
  → do NOT include gpu_platform.hpp (Vulkan)
  → forward-declare or gpu_pub_lib.h / future host-hooks header

Platform / CMake / Linux-only change?
  → incomplete until MSVC sln product build is green (dual-track)
```

Lessons and include table: [platform.md](platform.md).

---

## How to use this file

1. Pick the next **P0** item and implement against the owning [layer doc](layers/README.md) goals/non-goals.  
2. When something ships, move it under **Done** (one line) and renumber if needed.  
3. Do not revive July design PR checklists as live tasks — update this roadmap instead.
