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
| Client-rect resize / swapchain recreate | Host borderless fullscreen (app HWND) |

## Current surface

| Path | Role |
|------|------|
| `graphics_system.*` | Concrete `IGraphicsSystem` — lifecycle, init/resize, `record_command_buffer` glue |
| `gpu_pub_lib.h` | `EngineCreateInfo`, `GpuInstance`, `CameraUBO`, `FrameSubmit`, `SceneViewFilters`, pick types |
| `gpu_prv_lib.h` | Vulkan free-function surface (private) — implemented under `core/` |
| `core/` | Instance/device/swapchain, buffer/image/sampler, shader/pipeline/descriptor (Vulkan lifetime primitives) |
| `engine_requirements.*` | Required features + optional query (`DeviceOptionalFeatures`: mesh shaders); `create_device` enables `VK_EXT_mesh_shader` when usable |
| `pipeline.cpp` / `descriptor.cpp` / `image.cpp` | Shared `PipelineType` PSOs, descriptor layout/pool/write, `cmd_image_barrier` |
| `sampler.*` | `SamplerTable` by `SamplerFilter` index (shared; not pass-private) |
| `frame/` | Sync, resources, presenter, descriptors, swapchain targets, task graph, IPass nodes |
| `frame/frame_graph/` | `IPass`, `FrameTaskGraph` (DAG nodes + image barrier edges) |
| `frame/passes/` | Concrete `IPass`: `MainScenePass`, `InstanceCullPass` (GPU frustum → compact + indirect), `PickerPass`, `OutlinePass`, `SobelComputePass`, `EdgeOverlayPass` + `SobelResources` |
| `frame/profiling/` | `TimestampQueryPool` + `FrameProfiler` (CPU scopes + GPU timestamps; F3 HUD) |
| `frame/frame_loop.cpp` | `render_loop` / `render` (prepare → record → submit/present) |
| `frame/sobel_async_pass.*` | Multi-queue executor (_3D outline → CMP → _3D overlay) + fence; not a PSO owner |
| `frame/sobel_types.hpp` | Thin request type; outline list is `SobelOutlineInstance` in `gpu_pub_lib.h` |
| `frame/gpu_frame_publish.cpp` | Triple-buffer `publish_frame` / `apply_published_frame` |
| `frame/selection_state.cpp` | Selection, hover, multi-tx filter, detail refill pin |
| `frame/frame_shared_state.*` | Debug drawer / mesh arena / viewProj shared by loop + record |
| `frame/screenshot.cpp` | F12 / `request_screenshot`: GPU readback **while swapchain image is still acquired** (copy before present); window blit opt-in only |
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
  IFrameSource::prepare → cubes, sobel_outlines, debug (arrows + segment planes)
  publish_frame / upload
  InstanceCullPass (compute in graphics CB): frustum cull → compact SSBO + indirect args
  MainScenePass cubes:
    mesh path (if VK_EXT_mesh_shader enabled): DrawMeshTasksEXT from instance SSBO
    classic path: VBO/IBO DrawIndexed / DrawIndexedIndirect after cull
  optional PickerPass (pre-cull instance buffer; policy stays on GS)
  optional SobelAsyncPass executor:
    OutlinePass → CMP SobelComputePass → EdgeOverlayPass → present
  present (non-Sobel path)
```

### IPass + task graph

Every render pipeline is an **`IPass`**: private PSO create/destroy helpers, public `create` / `destroy` / `record` / `declare_resources`.  
`FrameTaskGraph::register_pass(IPass&)` plugs nodes; dependency edges carry image access transitions. Multi-queue submit stays in **`SobelAsyncPass`** (not inside IPass). Shared Sobel images/descriptors/semaphores live on **`SobelResources`** (attached by Outline / Compute / Overlay).

**Order note:** segment barrier planes are debug quads in the main pass. Sobel edge composite runs **after** planes (and ImGui) as a depth-free overlay so selection/TRACE edges read as a highlight on top of translucent planes.

### Sobel (domain-agnostic, single pass)

- App builds `std::vector<SobelOutlineInstance>` (`instance_index` + `color`); graphics has **no** role names (gold/green/cyan/orange).
- All outline cubes drawn in **one** private depth+color pass (scene depth unused).
- Compute Sobel produces a **white** edge mask; overlay multiplies `edge × instance_color` onto swapchain **after** main color (cubes + planes).
- Kill-switch `visualize_confirmed_tips` is passed to the presenter as `enable_role_outlines` (selection gold still emitted when selected).
- Product colors live in `ScenePresenter` (brand palette); see [app.md](app.md).

Validation: follow `.grok/skills/vulkan-validator` before commit/push of graphics changes.

## Goals

1. Pure GPU concerns: device, swapchain, pipelines, upload, present.
2. Validation-clean async Sobel (fence + queue serialization).
3. Latest-wins triple-buffer publish; pick returns `instance_index` + `frame_seq`.
4. ImGui host only; chrome via `IUiOverlay` on the render thread.
5. Fast rebuilds: PCH, `/MP`, incremental shaders — [build-performance.md](../build-performance.md).
6. Public header remains Vulkan-free.
7. Fullscreen is **host-owned** (borderless HWND); graphics only follows client size via `Resize()`.

## Non-goals

| Item | Reason |
|------|--------|
| REST / main-chain cache / explorer links | **Network** / **app** |
| Deciding green vs unconfirmed-red vs orange meaning | **App** presenter |
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
| **P1** | Frame pass profiler (CPU scopes + GPU timestamps) | `frame/profiling/*`, `FrameTimingSnapshot` in `gpu_pub_lib.h`; F3 HUD; `enable_frame_profiler` on engine |
| **P1** | VnV bench on profiler snapshots | `vnv/bench/` — median/p95 baselines via `run_vnv.ps1 -Bench` (opt-in, not in `-All`) |
| **Done (P2)** | Pipeline/descriptor/barrier modularization | `PipelineType`, `descriptor.cpp`, `cmd_image_barrier`, MeshArena shared PSOs |
| **Done (P2)** | Split `GraphicsSystem` orchestration | `frame_loop`, `async_sobel_submit`, `gpu_frame_publish`, `selection_state` |
| **P2** | Always-on tip Sobel perf budget | Soft median regression from bench; kill-switch escape hatch — **data-driven via profiler** |
| **P1 (V1 landed)** | Visual regression harness | `vnv/int/tests/visual/` + `run_vnv.ps1 -Int` (GPU readback + golden); more cases optional |
| **P3** | Full PIMPL of `GraphicsSystem` | Only if external includes force it |

### Frame profiler (quick ref)

| Piece | Role |
|-------|------|
| `TimestampQueryPool` | Per in-flight `VK_QUERY_TYPE_TIMESTAMP` pools; `timestampPeriod` → ms |
| `FrameProfiler` | Named CPU RAII scopes + GPU begin/end; rolling ring; bound class |
| Scopes | CPU: `Prepare`, `PublishUpload`, `MeshArenaUpload`, `RecordMain`, `SubmitPresent`, `OverlayUi`; GPU: DAG names + `Cubes` / `DebugMesh` / `ImGui` |
| Public API | `IEngine::enable_frame_profiler` / `copy_frame_timing_snapshot` (Vulkan-free) |
| HUD | F3 toggles sampling + ImGui table |

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
