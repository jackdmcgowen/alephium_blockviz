# Layer architecture

Living docs for **alephium_blockviz** after modularization. Prefer these over July 2026 design drafts when deciding where code belongs.

Versions (see `src/app/app_identity.hpp`, `src/engine/engine_identity.hpp`, `AGENTS.md`):

| Component | Identity | Tag pattern |
|-----------|----------|-------------|
| Host app | Alephium BlockFlow | `app-vMAJOR.MINOR.PATCH` |
| Product engine | BlockvizEngine | `engine-vMAJOR.MINOR.PATCH` |

Current identities: **app 0.4.0 / engine 0.5.0**. Tagged `main` may lag until release push (`AGENTS.md`).

---

## Map

```text
main / Win32 / config.json
        │
        ▼
   app (overlay, ScenePresenter, camera)
        │  IEngine
        ▼
   engine (BlockVizEngine, ISystem registry)
     ├── network (poll, adapter, confirm) ──► domain BlockScene
     └── graphics (render thread, Vulkan)
              ▲
              └── IFrameSource::prepare (app ScenePresenter)
```

| Layer | Project | Doc |
|-------|---------|-----|
| **App** | `alephium_visualizer` | [app.md](app.md) |
| **Engine** | `blockviz_engine` | [engine.md](engine.md) |
| **Graphics** | `graphics` | [graphics.md](graphics.md) |
| **Network** | `network` | [network.md](network.md) |

### Domain (shared data plane — not a full layer doc)

`src/domain/` holds chain-agnostic graph/scene/layout:

- `BlockScene` — live blocks, confirmed bag / frontier `H_c`, uncles, mutex, network HUD bag
- `BlockGraph` — metadata graph + optional edges
- `PolarShardLayout` — 16-lane polar positions
- `alph_block.hpp` — Alephium parse model used by network + inspector

**Writers:** network (ingest, marks). **Readers:** app `ScenePresenter` under scene mutex; graphics may hold `BlockScene*` for selection detail.

---

## Confirmation responsibilities

| Concern | Layer |
|---------|--------|
| API `is_main`, seed queue, `H_c` advance, `mark_confirmed` dual-write | [network](network.md) |
| Solid / green / cyan / orange / gold / red presentation | [app](app.md) |
| Multi-instance Sobel GPU path, gold vs green overlay | [graphics](graphics.md) |

---

## Related docs

| Doc | Status |
|-----|--------|
| [ROADMAP.md](../ROADMAP.md) | **Active** — ordered priority backlog (“what else”) |
| [build-performance.md](../build-performance.md) | **Active** — PCH, `/MP`, shaders, new systems |
| [graphics-modularization-design.md](../graphics-modularization-design.md) | **Historical (landed)** |
| [blockflow-confirmed-tips-design.md](../blockflow-confirmed-tips-design.md) | **Historical (landed & evolved)** |
| [AGENTS.md](../../AGENTS.md) | Version tags on `main` push |
| [.grok/skills/vulkan-validator](../../.grok/skills/vulkan-validator/SKILL.md) | Pre-push graphics validation |

---

## Smoke checklist

After meaningful changes:

1. Launch → discrete GPU name / engine identity logged  
2. Cubes within one poll interval (or Debug simulator when shipped)  
3. Select block → inspector has hash / deps / txns (or refill)  
4. Esc / close → clean shutdown (systems stop before HWND destroy)  
5. Resize window (swapchain recreate)  
6. Mainnet ↔ Testnet switch from Network panel  
7. Debug build: no new Vulkan VUIDs on the path you touched  

---

## Build (short)

```text
install_deps.bat
# Open sln/alephium_visualizer.sln — Debug|x64 or Release|x64
# Run from repo root so config.json and resource/ resolve
```

Deps: vcpkg manifest (`curl`, `cjson`, `glm`); Vulkan SDK; MSVC.
