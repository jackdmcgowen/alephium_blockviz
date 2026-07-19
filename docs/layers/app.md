# App layer

Host process and product presentation for **Alephium BlockFlow** (`alephium_visualizer`).

## Overview

The app owns the Win32 window, `config.json` load, wiring of systems into `IEngine`, ImGui chrome (`BlockflowOverlay`), render-thread camera (`CameraController`), and the production visual model (`ScenePresenter` as `IFrameSource`). It does not open sockets or create Vulkan devices.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| `main` / `WndProc`, process lifetime | Vulkan handles, swapchain |
| `config.c` + domain URL list | curl / REST command table |
| `BlockflowOverlay` (Network + Block panels) | Poll watermark, `is_main` |
| `ScenePresenter` visual policy | Graphics pipelines / Sobel barriers |
| `CameraController` timeline UX | Network thread lifecycle |
| Host identity (`app_identity.hpp`) | Engine version tags |

## Current surface

| Path | Role |
|------|------|
| `src/app/main.cpp` | Window, config boot, system register, message pump |
| `src/app/blockflow_overlay.*` | `IUiOverlay`: domain combo, loading HUD, feed, inspector |
| `src/app/scene_presenter.*` | `IFrameSource::prepare` — instances, arrows, highlight hash lists, `UiSnapshot` |
| `src/app/camera_controller.hpp` | Z-track attach/detach, LMB look, RMB pan, selection look-aim |
| `src/app/ui_snapshot.hpp` | Render-thread-safe UI bag (no live scene reads in overlay) |
| `src/app/config.c` / `config.h` | Load URL array from `config.json` |
| `src/app/app_identity.hpp` | App name + semver → `EngineCreateInfo.application` |
| `src/app/ui_chrome.hpp` | Shared chrome helpers |

**Threading:** Main thread = Win32 + idle sleep. Overlay `draw()` and `ScenePresenter::prepare` run on the **graphics render thread**. Overlay must only read `UiSnapshot` / engine selection APIs, not live `BlockScene` without a published snapshot.

### Visual model (production)

Documented in `scene_presenter.hpp` — keep layout spacing stable:

| Cue | Meaning |
|-----|---------|
| **Solid α** | Confirmed bag with all deps live |
| **Green** | Per-lane frontier tip `H_c` (or walk-anim display) + full `blockDeps` arrows |
| **Cyan** | Unconfirmed height &gt; `H_c` that deps a domain frontier tip + link arrows into tip |
| **Orange** | Missing-dep incompletes (not green/cyan) |
| **Gold** | Selection (Sobel + ephemeral arrows) |
| **Red** | Removal death fade |

Presentation only — confirmation marks come from **network** into `BlockScene`.

## Goals

1. Readable BlockFlow frontier at a glance (table above).
2. Network + Block panels without racing live scene (`UiSnapshot`).
3. Hot-switch Mainnet / Testnet; honest UX for reserved **Debug** domain.
4. Camera timeline UX: auto-follow tip, detach on scroll, reattach, look-at selection.
5. Keep product color / filter policy out of graphics Vulkan code.
6. Ship host version via `app_identity` + `app-v*` tags on `main` (`AGENTS.md`).

## Non-goals

| Item | Reason |
|------|--------|
| Offline simulator backend | **Network** implements FakeChain / Debug; app only enables the combo |
| Multi-chain marketplace UI | Product non-goal for this wave |
| Vulkan validation / PSO ownership | **Graphics** |
| Structured logging framework | printf + ImGui remain OK |
| Dual gold+green Sobel | Confirmed-tips non-goal; gold wins when selected |
| Green cube body fill / full main-chain paint | Outline + arrows only |

## Plan

| Priority | Item | Notes |
|----------|------|--------|
| **P0** | Keep this visual model + color legend aligned with code | Update when `ScenePresenter` semantics change |
| **Done** | Enable Debug domain in overlay | FakeChain selectable in Network panel |
| **P1** | Config persistence | Last domain, multi-tx filter, optional layout meters (modularization PR12 remainder) |
| **P2** | Confirm polish | Feed-row confirmed badge; eye-check green vs shard palette; dual outline only if product asks |
| **P2** | History / LOD presentation | Modes on top of segment cull / barrier planes |

## Interfaces

**Outbound**

- `create_engine` / `destroy_engine`
- Register `IGraphicsSystem` + `INetworkSystem`, then `init_systems` / `start` / `stop` / `free_systems`
- `set_scene`, `set_camera`, `set_frame_source`, `set_ui_overlay`
- `switch_network_domain(domain, url)` from Network panel

**Inbound**

- `UiSnapshot` via `copy_ui_snapshot` / fields filled in `prepare`
- Pick / selection / detail via engine facade
- Network HUD fields embedded in snapshot (status, lookback windows, frontier lanes)

**Domain**

- `main` owns `BlockScene` lifetime; presenter holds a reference

## Related

- [layers/README.md](README.md) — map
- [network.md](network.md) — marks and phases
- [graphics.md](graphics.md) — Sobel execution
- [engine.md](engine.md) — facade
- Historical: [blockflow-confirmed-tips-design.md](../blockflow-confirmed-tips-design.md) (landed & evolved)
