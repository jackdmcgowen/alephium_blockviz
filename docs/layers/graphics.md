# Graphics layer

Vulkan backend library **graphics** (`src/graphics/`). Implements `IGraphicsSystem`; owns the **render thread**.

## Overview

Graphics bootstraps instance/device/swapchain, records frames (cubes, debug draw, ImGui host, optional async Sobel), and presents. Public types in `gpu_pub_lib.h` stay **Vulkan-free**. Private helpers and `GraphicsSystem` are internal to the lib — host and network must not include `graphics_system.hpp`.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| Vulkan lifecycle, queues (`_3D` / `TX` / `CMP`) | Alephium REST / `is_main` |
| Render thread, frame publish slots | Explorer URLs, feed chrome |
| Pipelines, mesh arena, descriptors, barriers | Product color semantics (green/cyan/orange) |
| Pick pass + Sobel compute / overlay | Poll watermark |
| ImGui **host** (backends NewFrame / Render) | Overlay widget layout (app) |
| `gpu_pub_lib.h` public types | curl / cJSON |

## Current surface

| Path | Role |
|------|------|
| `graphics_system.*` | Concrete `IGraphicsSystem` — lifecycle, init/resize, `record_command_buffer` glue |
| `gpu_pub_lib.h` | `EngineCreateInfo`, `GpuInstance`, `CameraUBO`, `FrameSubmit`, pick types, `IUiOverlay` |
| `gpu_prv_lib.h` | Vulkan free-function helpers (private): pipeline, descriptor, image barriers |
| `pipeline.cpp` / `descriptor.cpp` / `image.cpp` | Shared `PipelineType` PSOs, descriptor layout/pool/write, `cmd_image_barrier` |
| `frame/` | Sync, resources, recorder, presenter, descriptors, picker, Sobel, swapchain targets, task graph |
| `frame/frame_loop.cpp` | `render_loop` / `render` (prepare → record → submit/present) |
| `frame/async_sobel_submit.cpp` | Multi-CB async Sobel submit + fence serialization |
| `frame/gpu_frame_publish.cpp` | Triple-buffer `publish_frame` / `apply_published_frame` |
| `frame/selection_state.cpp` | Selection, hover, multi-tx filter, detail refill pin |
| `frame/frame_shared_state.*` | Debug drawer / mesh arena / viewProj shared by loop + record |
| `frame/screenshot.cpp` | Client-area PNG capture (F12 / request_screenshot) |
| `pipelines/` | Cube + picker pipeline objects |
| `mesh_arena.*`, `buffer_manager.*` | Mesh / buffer pooling |
| `debug/debug_drawer.*` | Arrows and debug geometry |
| `shaders/*.glsl` + `compile_shaders.py` | SPIR-V pre-build |
| `camera.*` | View/proj helpers used by UBO path |
| `validation.cpp` | Debug layers (`#ifndef NDEBUG`) |

**Factory:** `create_graphics_system` / `destroy_graphics_system` (declared in `engine/engine.hpp`).

### Frame path (simplified)

```text
render thread (frame_loop.cpp):
  apply published GpuFrameSlot (gpu_frame_publish.cpp)
  IFrameSource::prepare → new FrameSourceOutput
  publish_frame / upload
  record main pass (+ debug arrows)  // graphics_system::record_command_buffer
  optional async Sobel (async_sobel_submit.cpp)
  optional pick pass
  IUiOverlay::draw → ImGui render
  present
```

### Sobel modes (product-facing kill-switch)

- **Selection gold** when selected instance maps; always attempted if Sobel ready.
- **Confirmed tips green** when unselected, tips resolve, and `visualize_confirmed_tips` is on (default on).
- MVP: one highlight color/buffer per frame — **no dual gold+green**.

Validation: follow `.grok/skills/vulkan-validator` before commit/push of graphics changes.

## Goals

1. Pure GPU concerns: device, swapchain, pipelines, upload, present.
2. Validation-clean async Sobel (fence + queue serialization).
3. Latest-wins triple-buffer publish; pick returns `instance_index` + `frame_seq`.
4. ImGui host only; chrome via `IUiOverlay` on the render thread.
5. Fast rebuilds: PCH, `/MP`, incremental shaders — [build-performance.md](../build-performance.md).
6. Public header remains Vulkan-free.

## Non-goals

| Item | Reason |
|------|--------|
| REST / main-chain cache / explorer links | **Network** / **app** |
| Deciding green vs cyan vs orange meaning | **App** presenter |
| Dual simultaneous gold and green Sobel | Confirmed-tips non-goal |
| Rewrite to D3D / OpenGL / WebGPU | Stack non-goal |
| Dual-GPU presentation deep-dive | Discrete pick is enough |
| Structured logging framework | printf OK |

## Plan

| Priority | Item | Notes |
|----------|------|--------|
| **P0** | Keep module map accurate as files move | Updated for frame_loop / selection / publish / async Sobel TUs |
| **P0** | Enforce public vs private include rules | App never includes `graphics_system.hpp` or `frame/*` internals |
| **P1** | Document kill-switch + highlight mode matrix | Config/runtime toggle surface |
| **P1** | Validation pre-push discipline | vulkan-validator skill |
| **Done (P2)** | Pipeline/descriptor/barrier modularization | `PipelineType`, `descriptor.cpp`, `cmd_image_barrier`, MeshArena shared PSOs |
| **Done (P2)** | Split `GraphicsSystem` orchestration | `frame_loop`, `async_sobel_submit`, `gpu_frame_publish`, `selection_state` |
| **P2** | Always-on tip Sobel perf budget | Soft median regression target from historical design; kill-switch escape hatch |
| **P3** | Full PIMPL of `GraphicsSystem` | Only if external includes force it |

## Interfaces

**Implements:** `IGraphicsSystem` (configure, lifecycle, submit/publish frame, pick, selection, UI snapshot, scene/camera/frame source wiring).

**Consumes:**

- `IFrameSource*` (app presenter)
- `CameraController*`, `BlockScene*` (selection detail / refill coordination — coupling documented as v1)
- `IUiOverlay*` (not owned)

**Publishes:** GPU frames; pick results; runs overlay on render thread.

## Related

- [layers/README.md](README.md)
- [engine.md](engine.md) — facade contracts
- [app.md](app.md) — visual meaning of highlights
- [build-performance.md](../build-performance.md)
- Historical: [graphics-modularization-design.md](../graphics-modularization-design.md), [blockflow-confirmed-tips-design.md](../blockflow-confirmed-tips-design.md) (Sobel multi-draw section)
