# Domain layer

Shared **data plane** for BlockFlow visualization (`src/domain/`). Not a static lib by itself — sources compile into the host app (`block_scene`, `block_graph`, `layout`) while `AlphDetailStore` lives under `network` but is owned through `BlockScene`.

## Overview

Domain holds chain-agnostic graph metadata, the live scene (confirmed frontiers, uncles, feed, HUD bag), and polar layout. **Network** writes; **app** `ScenePresenter` reads under the scene mutex; **graphics** may hold a `BlockScene*` for selection detail only.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| `BlockGraph` nodes/edges, prune helpers | Vulkan, ImGui chrome |
| `BlockScene` confirmed bag / `H_c` / feed / network HUD copy | REST / `is_main` policy |
| `PolarShardLayout` positions + session Z origins | Poll watermark |
| `alph_block.hpp` parse model (shared) | Curl lifecycle |

## Current surface

| Path | Role |
|------|------|
| `block_graph.*` | Metadata graph + optional edges; `prune(min_ts, max_nodes)` |
| `block_scene.*` | Ingest, confirm, uncles, feed, HUD, detail store, scene-level `prune` |
| `layout.*` | 16-lane polar layout; `txn_count` on placements |
| `alph_block.hpp` | Alephium block/txn parse + constants (`ALPH_*`) |

### Confirm vs present

| Concern | Layer |
|---------|--------|
| `mark_confirmed` / dual-write | **network** |
| Solid / green / cyan / orange drawing | **app** |
| Sobel multi-draw | **graphics** |

### Retention (`BlockScene::prune`)

- Drops nodes older than `min_timestamp_ms` and/or excess over `max_nodes`.
- **Protects** per-lane confirmed tips, pending tips, and frontier-walk hashes.
- Removes matching detail-store entries, confirmed bag membership, uncles, feed rows.
- Invoked from the network poller (~2× lookback, cap 12k) and FakeChain (soft cap).

## Goals

1. Single source of truth for live block identity + layout inputs.
2. Thread-safe scene mutex for network write / presenter read.
3. Confirmation state without putting Alephium policy in the graph node type.
4. Bounded memory via prune (not unbounded session growth).
5. Layout parity for 16-shard polar presentation.

## Non-goals

| Item | Reason |
|------|--------|
| HTTP / main-chain API | **network** |
| Cube shaders / pick images | **graphics** |
| Explorer URLs / chrome | **app** |
| Full ECS | Overkill |

## Plan

| Priority | Item | Notes |
|----------|------|--------|
| **Done** | Scene confirm + frontier heights | Evolved past original tips design |
| **Done** | Scene-level prune + poller/FakeChain hooks | Protects frontier tips |
| **P1** | Stronger GC of non-tip `confirmed_` bag | Optional; bag can still grow with marks |
| **P2** | First-class graph edge visualization inputs | App product decision (ROADMAP) |

## Interfaces

- **In:** `add_block` / `remove_block` / `mark_confirmed` / `mark_uncle` / `set_network_hud` / `prune`
- **Out:** snapshots for layout, locked confirm queries for presenter, `detail_store` for inspector

## Related

- [layers/README.md](README.md)
- [network.md](network.md) · [app.md](app.md)
- Tests: `tests/domain_tests.cpp`
