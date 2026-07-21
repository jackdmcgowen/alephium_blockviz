# Roadmap / priority backlog

Ordered “what else” for **alephium_blockviz**. Living layer goals: [layers/README.md](layers/README.md).  
Historical designs are archives, not the backlog: [modularization](graphics-modularization-design.md), [confirmed tips](blockflow-confirmed-tips-design.md).  
Platform / Linux layout: [platform.md](platform.md) · [linux.md](linux.md).

**Last updated:** 2026-07-21  
**Versions:** app **1.0.0** · engine **1.0.0** (see identity headers / tags on `main`)

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
| **VnV framework** (mod/int/bench + dual slns) | `vnv/` · `scripts/sync_solutions.ps1` · `run_vnv.ps1` |
| **PCH include audit** (`scripts/check_pch.py`) | CI + AGENTS |
| CRT helpers (`common/time_util.hpp`, `env_util.hpp`) | Portable local_time / env flags |
| **GPU screenshot primary**; window blit opt-in | `BLOCKVIZ_SCREENSHOT_WINDOW_BLIT=1` |
| **Multi-platform goldens** + headless CI hard gate | `goldens/linux_headless/` · `compare_images.py --profile` |
| **Dual-track smoke** script | `scripts/smoke_dual_track.sh` |

---

## P0 — Next (recommended order)

| # | Item | Layer | Why |
|---|------|-------|-----|
| 1 | **Merge `integration/platform/linux/01` → main** after Windows MSVC product smoke | build | Dual-track: Linux CI ≠ MSVC sln (PCH + include-isolation incidents) |

---

## P1 — Soon

| # | Item | Layer | Why |
|---|------|-------|-----|
| 2 | **Include-boundary audit** (extend `check_pch.py` or sibling) | build | Fail if `src/app/**` includes `gfx_platform.hpp` / `vulkan/vulkan.h` |
| 3 | **Vulkan-free host hooks header** | graphics · app | Split `configure_headless` (etc.) out of `gfx_platform.hpp` — app can include without Vulkan |
| 4 | Richer **dep-viz modes** (LOD / filters) | [app](layers/app.md) | Selection BFS shipped; avoid edge soup |
| 5 | **Confirm polish** (feed badges, green vs shard eye-check) | [app](layers/app.md) | Post-MVP from confirmed-tips design |
| 6 | **WebSocket** tip stream | [network](layers/network.md) | Lower latency; focused feature |

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

---

## Out of scope (do not schedule)

| Item | Reason |
|------|--------|
| Dual gold **and** green Sobel same frame | Confirmed-tips non-goal |
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
  → do NOT include gfx_platform.hpp (Vulkan)
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
