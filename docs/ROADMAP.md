# Roadmap / priority backlog

Single ordered backlog for **alephium_blockviz**.  
**Code map:** [layers/](layers/README.md) · **Platform:** [platform.md](platform.md) · [linux.md](linux.md) · **Product notes:** [planning/](planning/README.md)

**Last updated:** 2026-07-24  
**Versions on `main`:** app / engine **1.5.0** (see identity headers / tags).

| Status | Meaning |
|--------|---------|
| **P0** | Do next |
| **P1** | Soon |
| **P2** | Later / optional |
| **Out** | Explicit non-goal |

Shipped history lives in **git tags** and layer docs — not here.

---

## P0 — Next

| # | Item | Layer | Why |
|---|------|-------|-----|
| 1 | **Merge integration tip → `main`** (docs + motion) when ready | release | Motion/docs on planning branch not yet released |
| 2 | **Prepare amortization** / shorter critical section | app · domain | Dense N FPS limit is host prepare, not GPU — [perf](perf-bottlenecks.md) |
| 3 | **Vulkan-free host hooks header** | graphics · app | App includes hooks without pulling Vulkan |

---

## P1 — Soon

| # | Item | Layer | Why |
|---|------|-------|-----|
| 4 | **Default arrow budget** + “show all deps” | [app](layers/app.md) | Hierarchy; less edge soup |
| 5 | **Cold-start coach** (3-step: lanes / Z / green tip) | app | First 30s readability |
| 6 | **Confirm polish** (feed badges, green vs shard eye-check) | [app](layers/app.md) | Trust at a glance |
| 7 | **WebSocket** tip stream | [network](layers/network.md) | Lower latency |
| 8 | **DrawMeshTasksIndirect** from cull count | graphics | Drop empty mesh workgroups |

---

## P2 — Later

| # | Item | Layer | Why |
|---|------|-------|-----|
| 9 | Materials / palette polish (tokens first) | app · graphics | Art without layout redesign |
| 10 | “Why green?” inspector trust click | app | Height / bag / explorer |
| 11 | Screenshot mode (hide rails) | app | Share |
| 12 | History presentation LOD | app · network | Ring RAM OK; presentation open |
| 13 | Second chain adapter | network | After multi-adapter wiring |
| 14 | CMake-primary Windows; optional GLFW-only host | build · platform | One target graph |
| 15 | Full IPass executor / multi-queue | graphics | Topology registered; still special-cased |
| 16 | VnV path rename (`mod`→unit, etc.) | vnv | Docs already use taxonomy |

---

## Out of scope

| Item | Reason |
|------|--------|
| Dual exclusive selection-gold vs tip-green *modes* | Gold wins when selected; multi-role outlines intentional |
| Green cube body fill / full main-chain paint | Outline + arrows product |
| D3D / OpenGL / WebGPU rewrite | Stack non-goal |
| Full ECS, plugins, scripting | Overkill |
| Multi-chain marketplace UI | Product non-goal |
| Production auth / multi-endpoint HA | REST poll enough for v1 |

---

## Portability (short)

```text
OS / WSI / paths / process / CRT  →  src/<layer>/platform/* or common/*
Else                              →  domain / engine / network / app
App must not include Vulkan / gpu_platform.hpp
Platform change incomplete until MSVC product build is green
```

Details: [platform.md](platform.md) · [AGENTS.md](../AGENTS.md).

---

## How to use

1. Take the next **P0** against the owning [layer doc](layers/README.md).  
2. Ship → drop from this file (don’t grow a “Done” museum).  
3. Product critique / branch policy: [planning/](planning/README.md).
