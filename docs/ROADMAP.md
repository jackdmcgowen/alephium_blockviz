# Roadmap / priority backlog

Ordered “what else” for **alephium_blockviz**. Living layer goals: [layers/README.md](layers/README.md).  
Historical designs are archives, not the backlog: [modularization](graphics-modularization-design.md), [confirmed tips](blockflow-confirmed-tips-design.md).

**Last updated:** 2026-07-19  
**Versions:** app **0.4.0** · engine **0.5.0** (see identity headers)

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
| Living layer docs (app, engine, graphics, network) | [layers/](layers/README.md) |
| Scene UX + graphics pipeline modular work (feature branch) | multi-tx filter, billboards, dep hover, PSO/frame split — on `feature/graphics-pipeline-descriptor-modular` |
| Offline **Debug / FakeChain** simulator | [network](layers/network.md) `fake/fake_chain_simulator.*`; Network panel Debug selectable |
| Domain / detail **unit tests** | `tests/domain_tests.cpp` · `sln/blockviz_tests.vcxproj` |
| Borderless **fullscreen** (F11 / Esc exit FS) | App `window_fullscreen.hpp`; graphics resizes only |
| Graphics **visual regression** harness (V1) | `tests/visual/` + `run_visual_tests.ps1`; golden PNG compare |

---

## P0 — Next (recommended order)

| # | Item | Layer | Why |
|---|------|-------|-----|
| — | *(empty — previous P0 landed on feature branch with app 0.4 / engine 0.5)* | | Merge to `main` + push `app-v0.4.0` / `engine-v0.5.0` tags when ready ([AGENTS.md](../AGENTS.md)) |

---

## P1 — Soon

| # | Item | Layer | Why |
|---|------|-------|-----|
| 4 | **Config persistence** (last domain, filters) | [app](layers/app.md) | **Done on `feature/p1-config-persistence-prune`** — `user_prefs.json` |
| 5 | Graph + detail **retention / prune** policy | [domain](layers/domain.md) · [network](layers/network.md) | **Done on branch** — `BlockScene::prune`, poller + FakeChain hooks |
| 6 | **`docs/layers/domain.md`** | docs | **Done on branch** |

*(Mark fully Done in the Done table after merge to main.)*

---

## P2 — Later

| # | Item | Layer | Why |
|---|------|-------|-----|
| 7 | Richer **dep-viz modes** (selection / frontier / LOD) | [app](layers/app.md) | Product decision first; avoid full edge soup by default |
| 8 | **Confirm polish** (feed badges, green vs shard eye-check) | [app](layers/app.md) | Post-MVP open questions from confirmed-tips design |
| 9 | **WebSocket** tip stream | [network](layers/network.md) | Lower latency; focused feature, not a networking platform rewrite |
| 10 | **History depth + LOD** presentation | app · network | Builds on segment cull / camera history |
| 11 | **Second real chain** adapter | [network](layers/network.md) | Only after FakeChain proves multi-adapter wiring |
| 12 | Headless / client-driven **frame test seam** | [engine](layers/engine.md) · [graphics](layers/graphics.md) | Only if automated GPU / CI tests become a goal |

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

---

## How to use this file

1. Pick the next **P0** item and implement against the owning [layer doc](layers/README.md) goals/non-goals.  
2. When something ships, move it under **Done** (one line) and renumber if needed.  
3. Do not revive July design PR checklists as live tasks — update this roadmap instead.
