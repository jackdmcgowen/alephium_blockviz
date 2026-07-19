# Network layer

Ingest and confirmation library **network** (`src/network/`). Implements `INetworkSystem`; Alephium policy in `alephium/`.

## Overview

Network owns curl lifecycle, the poller thread, REST helpers (`commands.c`), and `AlephiumAdapter`: lookback poll, tip identify, per-lane confirm walk, steady poll, main-chain cache, fetch pool, and detail-store slim/refill. It writes graph + confirmation into `BlockScene`. It does **not** choose cube colors or run Vulkan.

## Ownership boundary

| Owns | Must not own |
|------|----------------|
| `INetworkSystem` / poller thread | Vulkan, render thread |
| REST poll watermark + overlap | ImGui chrome layout |
| `MainChainCache` (network thread only) | Product green/cyan/orange semantics |
| `mark_confirmed` dual-write into scene | Explorer URL chrome (app uses domain helper) |
| `AlphDetailStore` upsert / slim / pin | Cube layout math |
| Domain switch (Mainnet ↔ Testnet) | Graphics pipelines |

## Current surface

| Path | Role |
|------|------|
| `network_system.cpp` | Concrete `INetworkSystem` (curl global, start/stop poller, `switch_domain`) |
| `network_domain.hpp` | `NetworkDomain`, defaults, URL resolve, `NetworkStatus` labels |
| `commands.c` / `commands.h` | Alephium REST helpers |
| `alephium/network_poller.*` | Thread loop: poll / drain verify / HUD |
| `alephium/alephium_adapter.*` | Phase machine, confirm, flood, stats |
| `alephium/main_chain_cache.*` | `is_main` cache + hot-zone priority helpers |
| `alephium/block_fetch_pool.*` | Concurrent hash fetches |
| `alephium/alph_detail_store.*` | Full block detail + PR11 slim policy |
| `fake/fake_chain_simulator.*` | Offline Debug domain: synthetic 16-lane growth + confirms |

**Factory:** `create_network_system(BlockScene&, IEngine&)` / `destroy_network_system` (declared in `engine/engine.hpp`).

### Adapter phases

```text
BootstrapPoll → IdentifyTips → BfsTrace (parallel BFS) → Steady
```

| Phase | Behavior (summary) |
|-------|---------------------|
| **BootstrapPoll** | First lookback window poll |
| **IdentifyTips** | Establish tips / main-chain identity; poll may be gated |
| **BfsTrace** | Parallel BFS confirm (`N=2G−1` workers): phase A seeds diagonal tips `[g→g]` only; phase B seeds remaining tips only if not already visited. Cross-shard deps. Pool-only expand; live holes hash-fill, history time-slots. Restart when segment fully loads. |
| **Steady** | Interval polls; BFS maintenance + camera-unlock restart |

Additional policy themes (see header comments on `AlephiumAdapter`): sequential frontier height `H_c`, free-main dep propagation, chain-walk multi-step confirm, live vs historical fetch rules (history may restrict hash fetches).

**Live poll vs camera:** while lookback index `k > 0` (camera beyond the live segment), do **not** force-poll window 0 or start new live tip seeds; historical windows `1..k` still load. On return to `k == 0`, if `poll_interval` has elapsed since the last live window poll, force live tip-adjacent chunks and reseed tip verification (stay in Steady).

**Chunked timeline:** each lookback segment (default 10 min) is filled with budgeted **~60s** `blocks-with-events` GETs (newest-first). Steady live refresh re-requests only the **newest** chunk(s), not the full window. `drain_verify` pumps at most one chunk every ~400ms so blocks pop in between Steady polls. HUD `load_ratio` blends chunk progress with density.

**Dual-segment + tip priority:** Bootstrap high-budget fills **windows 0 then 1** before IdentifyTips. History **≥2** is gated until Steady and live window 0 is fully chunk-filled.

**Triple-buffer segment ring:** always **current + 2 ahead** `{k_eff, k_eff+1, k_eff+2}` (HUD older→newer). Prefetch hysteresis (~40% into current segment toward older) raises `k_eff` early; pump fills **ahead windows first**. Bootstrap includes index 2 ASAP. **Global index G** from genesis; windows genesis-aligned. Minimap Z-proportional to planes; page-older steps one segment and recenters toward the **right**.

**Cache:** keep graph/detail until process private memory ≈ **2 GB** (or soft node cap); no routine 2×lookback wipe. Evict oldest non-frontier under pressure.

**Timeline origin:** sticky session origin (not min loaded block ts) so attached camera Z does not jump when history admits.

### Domains

| Domain | Selectable | Notes |
|--------|------------|--------|
| Mainnet | Yes | Default URL / config match |
| Testnet | Yes | Hot switch supported |
| Debug | **Yes** | Offline `FakeChainSimulator` (`debug://fake-chain`); no HTTP |

### Confirmation bridge

- **Network thread** calls `BlockScene::mark_confirmed` (self-locking) on verify success, poll re-admit of cached main, replace paths, etc.
- **`MainChainCache` stays network-only** — never shared with the render thread.
- **App presenter** reads `is_confirmed_locked` / frontier helpers under the scene mutex already held in `prepare`.
- Erase confirmed on remove / uncle eviction (domain scene helpers).

### Detail store (PR11 — landed)

- Parse-time upsert of full `AlphBlock`.
- Slim unpinned txn payloads; pin selection for full detail; refill via engine/adapter when inspector needs payloads again.
- `txn_count` survives slim for billboards / multi-tx filter.

## Goals

1. Reliable BlockFlow ingest from config/node URLs with **success-only** watermark advance.
2. Proven main-chain confirmation in `BlockScene` (dual-write + poll re-admit).
3. Hot Mainnet ↔ Testnet switch that resets scene/poller without process teardown.
4. Inspector continuity (`AlphDetailStore` + refill).
5. Keep REST/cJSON/curl out of graphics and app chrome TUs.
6. HUD observability: status, lookback windows, frontier lanes, open walks (`publish_hud` → scene → snapshot).

## Non-goals

| Item | Reason |
|------|--------|
| Auth / retry / multi-endpoint HA platform | Modularization non-goal; REST poll is v1 |
| WebSocket as prerequisite | Optional later; REST default |
| Product visual semantics | **App** |
| Vulkan / render thread | **Graphics** |
| Second real chain in the same breath as FakeChain | Prove multi-adapter with Debug first |
| Changing uncle/orphan product policy in docs | Code policy stays until an explicit product change |
| Structured logging framework | printf + HUD OK |

## Plan

| Priority | Item | Notes |
|----------|------|--------|
| **Done** | Offline Debug / FakeChain simulator | `src/network/fake/fake_chain_simulator.*` |
| **Done** | Unit tests (no GPU) | `vnv/mod/tests/` / `mod_domain` via `run_vnv.ps1` |
| **Done** | Live poll vs camera lookback `k` | Skip window 0 while `k>0`; resync on return |
| **Done** | Chunked timeline interval polls | ~60s chunks, budgeted pump, newest-first |
| **Done** | Dual-segment bootstrap + triple-buffer ring | ≤3 active segments; admit-driven progress |
| **Done** | HttpIoPool + async interval GETs | Workers overlap timeline RTT; `mod_network` |
| **P1** | Async is_main / hashes via pool | Confirm path not RTT-serialized |
| **Done** | Retention / prune (branch) | Poller min_ts + soft node cap |
| **Standing** | Keep phases, dual-write, thread contract accurate | Hygiene when adapter policy shifts |
| **P1** | Config / URL resolution notes | `config.json` array + `network_domain_resolve_url` |
| **P2** | Optional websocket tip stream | Focused feature, not a networking rewrite |
| **P2** | Second real chain adapter | After FakeChain proves wiring |
| **P2** | Eye-centered chunk priority / adaptive chunk size | Optional polish on top of newest-first |

## Interfaces

**Inbound**

- Host/engine: `configure(NetworkSystemConfig)`, lifecycle, `switch_domain(domain, base_url)`
- App domain combo → `IEngine::switch_network_domain` → network system

**Outbound**

- `BlockScene`: add/remove blocks, `mark_confirmed`, network HUD bag, detail store
- Engine: selection detail refill coordination

**HTTP**

- `commands.c` against `base_url`; curl global init/cleanup in `NetworkSystem`

### Threading

| Thread | Work |
|--------|------|
| Network / poller | Policy: poll schedule, drain results, admit, confirm, cache, scene writes |
| **HttpIoPool workers** (default 6) | REST GETs only (interval chunks, block-by-hash, …); private curl / transport |
| UI / render | Must not call into `MainChainCache`; domain switch may **block** until poller restarted |

Timeline `blocks-with-events` chunks are **enqueued** to the pool (inflight cap ~4) so bootstrap/history RTT overlaps; poller **admits** on drain. VnV: `vnv/mod/tests/network/` (`mod_network`).

## Related

- [layers/README.md](README.md) — confirmation split table
- [app.md](app.md) — presentation of confirmed state
- [engine.md](engine.md) — `INetworkSystem` on facade
- Historical: [blockflow-confirmed-tips-design.md](../blockflow-confirmed-tips-design.md) (dual-write matrix still conceptually valid; paths evolved to `src/network/alephium/`)
- Historical: [graphics-modularization-design.md](../graphics-modularization-design.md) (adapter + detail store goals)
