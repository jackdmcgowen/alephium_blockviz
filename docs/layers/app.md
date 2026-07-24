# App layer

Host process and product presentation for **Alephium BlockFlow** (`alephium_visualizer`).

## Overview

The app owns the **host window** (Win32 on Windows, GLFW on Linux via `app/platform/*`), `config.json` / `user_prefs.json` load, wiring of systems into `IEngine`, ImGui chrome (`BlockflowOverlay`), render-thread camera (`CameraController`), and the production visual model (`ScenePresenter` as `IFrameSource`). It does not open sockets or create Vulkan devices.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| `main` + `app/platform/*` event loop, process lifetime | Vulkan handles, swapchain |
| `config.c` + domain URL list | curl / REST command table |
| `BlockflowOverlay` (Network + Block panels) | Poll watermark, `is_main` |
| `ScenePresenter` visual policy | Graphics pipelines / Sobel barriers |
| `CameraController` timeline UX | Network thread lifecycle |
| Host identity (`app_identity.hpp`) | Engine version tags |
| Borderless fullscreen (Win32 HWND / GLFW F11) | Exclusive display mode / Vulkan FS exclusive |

## Current surface

| Path | Role |
|------|------|
| `src/app/main.cpp` | Window, config boot, system register, platform pump; F11/Esc fullscreen |
| `src/app/platform/*` | OS host: Win32 or GLFW (see [platform.md](../platform.md)) |
| `src/app/window_fullscreen.hpp` | Borderless fullscreen enter/exit (Win32 style + placement) |
| `src/app/blockflow_overlay.*` | `IUiOverlay`: domain combo, loading HUD, feed, inspector |
| `src/app/scene_presenter.*` | `IFrameSource::prepare` — instances (opaque F2B sort), arrows, colored `sobel_outlines`, `UiSnapshot` |
| `src/app/camera_controller.hpp` | Z-track attach/detach, LMB look, RMB pan, selection look-aim |
| Timeline minimap (overlay) | **High-level multi-segment overview** (~24 newest `#G_seg`, **640s** G); **Z-proportional** bins (match cubes/planes); optional **64s** ticks on hover/camera cell; **click** bin → teleport; Live / key **3** → tip. **Must update whenever timeline constants change** (see `Agents.md`) |
| Camera view presets | **End** (1) / **Side** (2) / **V** toggle with pose memory. **3** = live tip. **L/R** = Z. Short **RMB** = deselect only (no reattach); RMB drag = pan |
| Selection + deps | **First-order (1-hop) only** from selected root. Gold arrows **instant on click** (no grow). **R** / Replay deps re-grows hop-1 only. 1-hop ghosts for missing direct deps. |
| Style tokens | `style_blockflow.hpp` + JSON — `walk_trace` ≠ gold; hop/sobel fade; `block_pop_*` / `wave_*` |
| Motion easing | `motion_easing.hpp` — admit pop-in (ease-out-back, N-cap); rare Z-wave (ease-in-out bump) |
| `src/app/ui_snapshot.hpp` | Render-thread-safe UI bag (no live scene reads in overlay) |
| `src/app/config.c` / `config.h` | Load URL array from `config.json` |
| `src/app/app_identity.hpp` | App name + semver → `EngineCreateInfo.application` |
| `src/app/ui_chrome.hpp` | Shared chrome helpers |

**Threading:** Main thread = platform message pump + idle sleep. Overlay `draw()` and `ScenePresenter::prepare` run on the **graphics render thread**. Overlay must only read `UiSnapshot` / engine selection APIs, not live `BlockScene` without a published snapshot.

**Dense N / FPS:** `prepare` holds `BlockScene` mutex and may rebuild **full polar layout** when the graph generation changes (every network admit). That host work is the usual framerate limit with thousands of blocks — not GPU overdraw. Prefer shorter critical sections and amortized layout (see [network.md](network.md#interaction-with-graphics--frame-rate), [perf-bottlenecks.md](../perf-bottlenecks.md)). Opaque instance front-to-back sort runs after build for early-Z only.

### Visual model (production)

Documented in `scene_presenter.hpp` — keep layout spacing stable:

| Cue | Meaning |
|-----|---------|
| **Solid α** | Confirmed bag with all deps live |
| **Green Sobel** | Per-lane frontier tip `H_c` (or walk-anim display) |
| **Tip dep arrows** | See **Rule book** below |
| **Red Sobel** | Unconfirmed roles (outline) — high contrast vs green main |
| **Orange** | Missing-dep incompletes (not green/unconfirmed red) |
| **Gold** | Selection (Sobel + ephemeral arrows; re-grows on select/Replay) |
| **Red (fade)** | Removal death α → 0 (same family as unconfirmed) |
| **Gray volume** | Queued **history** subsegment network fill (translucent AABB); α-fades when admitted (never live tip) |
| Segments rail | Compact “Segments (N)” line; **hover tooltip** for per-k load/blocks |

Presentation only — confirmation marks come from **network** into `BlockScene` (anchor tips + forward novelty: all-deps-Main ⇒ Main).

### Rule book — tip arrows & live segment planes

**Tip roles**

| Role | Who | Deps |
|------|-----|------|
| Primary confirmed | Domain `H_c` / walk hop | All live deps |
| Unconfirmed tip | Per-lane max-height unconfirmed in ring | All live deps |
| Secondary | Prior primary after tip advance | Full fan while retained |

**Tip arrow appearance (not selection gold)**

| State | Length | Color |
|-------|--------|--------|
| Primary | Full immediately (no grow) | Linear dual RGBA along axis: base=listing block, head=dep block (white if missing) |
| Unconfirmed → unconfirmed | Full immediately | **Red both ends** |
| Unconfirmed → confirmed | Full immediately | Red → main dual (color lerp) |
| Secondary | Full immediately | Main dual; α solid → translucent floor (&gt; 0) |
| Replaced | Full | Fade α → 0 then erase |
| Listing removed | Full | Red death α → 0 |
| Selection / hover | May grow | Gold; **only** these re-grow on select/Replay/hover |

**Segment rings (three-ring model)**

| Ring | Size | Role |
|------|------|------|
| **Render** | **7** G-windows, camera-centered (`cam_k±3`, clamped) | Cubes, tip arrows, Sobel, selection — **all non-plane draw** |
| **Load** | **15** G-windows, camera-centered (`cam_k±7`) | Disk-first body (adapter) + **barrier planes only** (presenter) |
| **Live poll** | last **~8s** of open G | Tip growth (not a separate G length) |

**Live segment barrier planes**

- **Do not** draw a plane for the open live segment interior (`k=0` / `G=G_live`).
- **Do** draw planes for completed boundaries (`k≥1`) **out to the load ring** (both directions).
- **No** cubes / arrows / outlines outside the **render** ring.
- When live G rolls, the previous live G becomes historical and then gets a plane.

**Frustum**

- Cull frustum near/far must match this frame’s camera UBO (clip set before prepare cull).

### Sobel colors (app → graphics)

Graphics is domain-agnostic: the presenter emits `std::vector<SobelOutlineInstance>` with `instance_index` + `rgba` (intensity in **a**). Product roles map to palette constants in `scene_presenter.cpp` (gold / tip green / frontier cyan / incomplete orange). Selection is exclusive (gold only); otherwise role outlines are co-visible in **one** GPU pass. Kill-switch arrives as `FrameSourceInput::enable_role_outlines`.

## Goals

1. Readable BlockFlow frontier at a glance (table above).
2. Network + Block panels without racing live scene (`UiSnapshot`).
3. Hot-switch Mainnet / Testnet; honest UX for reserved **Debug** domain.
4. Camera timeline UX: auto-follow tip, detach on scroll, reattach, look-at selection.
5. Side/front camera toggle with Side orbit; selection BFS dep fan; brand-sleek arrows/Sobel.

## Non-goals (presentation)

| Non-goal | Why |
|----------|-----|
| Dual / PiP cameras | One camera + presets |
| Multi-threaded animation workers for walks / BFS viz | API is the bottleneck; single render-thread tick |
| UI-driven parallel network dep traces | Adapter BFS remains policy-only (cooperative) |
| Full theming / CSS ImGui skin | Tokens + JSON only |
| Data-driving layout, network intervals, pipelines | Code-owned; only style + hop timing externalized |
| Auto-fetch missing walk deps during animation | Graph-local; missing hop = death |

## Style / timing data-drive

- Defaults: brand palette in `docs/brand/alephium_palette.md` + compiled `StyleBlockflow`.
- Optional override: `resource/style_blockflow.json` (loaded at presenter construct).
- **Do not** grow this into a general config system.
5. Keep product color / filter policy out of graphics Vulkan code.
6. Ship host version via `app_identity` + `app-v*` tags on `main` (`AGENTS.md`).
7. **Borderless fullscreen:** F11 toggles; **Esc exits fullscreen** (windowed Esc still quits). Graphics only resizes.

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
| **Done** | Config persistence | Last domain, multi-tx + min ALPH filters (`user_prefs.json`) |
| **Done** | Timeline minimap + segment jump | Overview bins; click → start; Live / key 3 |
| **Done** | ALPH out totals + amount filter | Sum txn outputs; billboard + inspector; min ALPH filter |
| **Done** | Layout/camera orientation | Lane 0→0 screen-right; LMB/RMB signs fixed |
| **P2** | Confirm polish | Feed-row confirmed badge; eye-check green vs shard palette; dual outline only if product asks |
| **P2** | History / LOD presentation | Modes on top of segment cull / barrier planes |

**Layout convention:** camera `up=(0,-1,0)` makes world **+X** screen-left; polar placement uses **−r·cos(θ)** so chain **0→0** sits on the viewer’s **right**.

**Timeline origin:** set once per session (`now − lookback`); never rewrite from earliest block (avoids attached-camera Z snaps on load).

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
