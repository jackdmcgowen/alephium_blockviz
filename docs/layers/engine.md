# Engine layer

Product shell library **blockviz_engine** (`src/engine/`). Vulkan-free facade between the host app and registered systems.

## Overview

`BlockVizEngine` (`IEngine`) owns an `ISystem` registry, forwards product APIs to `IGraphicsSystem`, and exposes convenience hooks for network domain switch. Frame contracts (`IFrameSource`, `FrameSourceInput` / `FrameSourceOutput`) live here so graphics and app share one header surface without pulling Vulkan.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| `IEngine` / `BlockVizEngine` lifecycle | `Vk*` types |
| `ISystem` registry + init/free/start/stop order | HTTP parse, cJSON |
| Frame-source types (`frame_types.hpp`) | ImGui chrome widgets |
| Public factory declarations for graphics + network | Pipeline / barrier details |
| Engine identity (`engine_identity.hpp`) | App window / `WndProc` |

## Current surface

| Path | Role |
|------|------|
| `src/engine/engine.hpp` | `IEngine`, `IGraphicsSystem`, `INetworkSystem`, `NetworkSystemConfig`, factories |
| `src/engine/engine.cpp` | `BlockVizEngine` implementation |
| `src/engine/frame_types.hpp` | `IFrameSource`, `FrameSourceInput` / `Output`, highlight hash lists |
| `src/engine/system.hpp` | `ISystem` — `name` / `init` / `free` / `start` / `stop` |
| `src/engine/engine_identity.hpp` | Engine name + semver (tags + logging) |

**Preferred includes:** leaf TUs should prefer `engine/frame_types.hpp` or `engine/system.hpp` over the full `engine.hpp` when possible.

### Lifecycle (host)

```text
create_engine()
  create_graphics_system → configure → add_system
  create_network_system  → configure → add_system
init_systems()
set_scene / set_camera / set_frame_source / set_ui_overlay
start()
  … run …
stop()
free_systems()
destroy_engine()
```

### Facade notes (v1)

- `publish_frame` carries `pick_map` plus highlight lists (`confirmed_tip_hashes`, `cyan_frontier_hashes`, `incomplete_hashes`) so GPU Sobel stays coherent with the published instance buffer.
- Domain types still appear on the facade (`BlockScene*`, `AlphBlock` for selection copy). Accepted for v1 so inspector refill works; optional future slim would move detail entirely behind opaque IDs.
- Render thread is owned by **graphics**, not by a client-driven `render_frame` loop.

## Goals

1. Stable, Vulkan-free product API for the host.
2. Uniform system lifecycle: configure → init → start / stop → free.
3. Coherent frame publish (instances + pick map + highlights paired).
4. Single host entry to product backend (`IEngine` only from `main` / overlay).
5. `engine-v*` tags match `engine_identity.hpp` on every `main` release (`AGENTS.md`).

## Non-goals

| Item | Reason |
|------|--------|
| ImGui chrome content | **App** `IUiOverlay` |
| GPU barriers / swapchain | **Graphics** |
| Chain JSON / REST | **Network** |
| ECS / plugin SDK | Overkill for this product |
| Client-driven `render_frame` for v1 | Testing seam only if appetite appears later |
| Owning confirmation policy | **Network** marks; **app** presents |

## Plan

| Priority | Item | Notes |
|----------|------|--------|
| **P0** | Keep facade surface documented against `engine.hpp` | Update when signatures change |
| **P1** | Include hygiene | Forward-declare; avoid pulling `engine.hpp` into pure graphics private TUs unnecessarily |
| **P1** | Document accepted domain leakage on facade | `AlphBlock` / `BlockScene*` — when to slim |
| **P2** | Optional headless / `render_frame` test seam | Only if automated GPU or integration tests need it |
| **P2** | New systems | Copy `sln/_template_system.vcxproj.example`; see [build-performance.md](../build-performance.md) |

## Interfaces

**Host → engine**

- Lifecycle + registry
- Selection, multi-tx filter, dep hover, UI snapshot publish/copy
- `switch_network_domain` / `network_domain` / `network_is_switching`

**Engine → systems**

- Owns pointers added via `add_system` (destroy on free)
- Forwards graphics methods to registered `IGraphicsSystem`
- Resolves `INetworkSystem` by name for domain switch

**Graphics → app (via contracts owned here)**

- Render thread calls `IFrameSource::prepare` then `publish_frame`

## Related

- [layers/README.md](README.md)
- [app.md](app.md), [graphics.md](graphics.md), [network.md](network.md)
- Historical modularization: [graphics-modularization-design.md](../graphics-modularization-design.md) (landed)
