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
| `alephium/segment_disk_cache.*` | Verified segment gzip cache under `%LOCALAPPDATA%`; bootstrap before network |
| `fake/fake_chain_simulator.*` | Offline Debug domain: synthetic 16-lane growth + confirms |

**Factory:** `create_network_system(BlockScene&, IEngine&)` / `destroy_network_system` (declared in `engine/engine.hpp`).

### Adapter phases

```text
BootstrapPoll → IdentifyTips → BfsTrace (parallel BFS) → Steady  (UI: "Stable")
```

| Phase | Behavior (summary) |
|-------|---------------------|
| **BootstrapPoll** | Live-first: exit when **live window tip-ready** (win1 best-effort; history gaps do not block) |
| **IdentifyTips** | Establish tips / main-chain identity; poll may be gated |
| **BfsTrace** | Parallel BFS confirm (`N=2G−1` **logical workers**, not OS threads): phase A diagonal tips; phase B remaining. Enters Steady without waiting for all dep fills (fills continue async). |
| **Steady** | Interval polls; BFS maintenance. HUD primary label **Stable**. History gaps / missing deps are a **secondary** status line, not Bootstrapping. |

**Status HUD:** primary = live product readiness (**Stable** / History / Catching up / …). Secondary = `status_detail` (e.g. filling deps, history incomplete near camera). Incomplete older chunks never demote Stable back to Bootstrapping.

Additional policy themes (see header comments on `AlephiumAdapter`): **anchor + forward novelty** for live main, free-main dep fill, live vs historical fetch rules.

**DAG missing fill:** budgeted walk from confirmed tips collects broken edges (parent in graph, dep missing). **≥2** unique missing → inclusive time range `[min_parent_ts, max_parent_ts]` (+ slack, split at max patch) on the priority hole queue. **Exactly 1** missing → sparse **single-hash queue**, drained only after DepCritical ranges are idle (so ranges can resolve clusters first). Exit path: host hides the window (`SW_HIDE`) before engine stop for responsive close.

**Live main-chain policy (simplified):**

1. **Anchor:** refresh network tip heights → tip hash per lane → mark Main and set green `H_c` (`mark_scene_anchor_`). Tip proof: hashes-at-height singleton and/or one `is_main` for the tip only.
2. **Forward novelty:** any admitted block whose **all known deps are already Main** is marked Main (`try_forward_promote_`) — **no** per-hop `is_main`.
3. **Green tip steps:** sequential `H_c+1` when that hash is Main; jump only via **network tip** anchor (not lagging bag).
4. **Spoof / conflict:** `is_main` / replace path only when tip is not main or duplicate competitor; not a safety-critical verifier (explorer remains deep truth).
5. **Disk bootstrap:** bag-only + `clear_sequential_frontiers`; live anchors re-establish green tips.

**Live poll vs camera:** while lookback index `k > 0` (camera beyond the live segment), do **not** force-poll window 0 or start new live tip seeds; historical windows `1..k` still load. On return to `k == 0`, if `poll_interval` has elapsed since the last live window poll, force live tip-adjacent chunks and reseed tip verification (stay in Steady).

**Chunked timeline:** shared **64s** subsegment grid (disk + HTTP); G-segment = **640s** (exactly 10 subsegments). Live tip poll = full open genesis cell `[floor(now), next_boundary)`. History GETs stay **strictly older** (`to ≤ live_open_from`). Each pump: **live first**, then fill **all incomplete subsegs in the visible admit/render ring (7 G)** ASAP (disk first, then network)—not a camera-local ±half-G time bubble. **429/5xx** → exponential backoff.

**Soft RAM eviction:** pressure prune outside the admit ring is **soft** (`BlockScene::prune(..., soft_evict=true)`). Presenter must **not** play red death VFX for those leaves (disk re-admit on return).

**Dual-segment + tip priority:** Bootstrap pumps windows 0 and 1, but **gates IdentifyTips on live window tip-ready** (not perfect dual history). Win1 continues in background. Live tip seeds / deep history still prefer Steady + live window 0 filled. **View/fetch ring always follows `cam_k`**.

**Terminology:** **G** = number of **groups** (`ALPH_NUM_GROUPS`, 4); shards = G×G lanes; each block has **2G−1** deps. Timeline **lookback k** is tip-relative (`k=0` live tip window, higher = older). Do not confuse k with groups. Window **ms bounds** are genesis-aligned (`G_live − k`); minimap Z uses those bounds so bars match cubes/planes.

**Three-ring segment model:**

| Ring | Size | Behavior |
|------|------|----------|
| **Load** | **15** G-windows, camera-centered | Disk-first (`try_fill` / bootstrap); network body only for holes. Chunked disk admit on boot (no bulk hitch). |
| **Render** | **7** (app) | Draw corridor only; see app rule book |
| **Live poll** | **64s** open tip subseg | Genesis-aligned open slot; history never overlaps it |

Fetch priority: live 8s edge → load-ring disk → load-ring network. History dep-hole ranges are **history-only** (not live tip path). Minimap labels genesis segment numbers (`#G_seg`).

**History mode:** when `cam_k ≥ 1` (live outside the sliding window), HUD **Status = History**; **halt** live tip growth, live force-poll, and new tip is_main seeds. Continue history ring ensure/pump + higher interval admit budget.

**Load-once 64s chunks:** successful policy admit (including valid empty time spans) marks the chunk forever. Re-GET only on HTTP/parse failure (forget completed), hard-prune invalidation, or live tip `force_newest` on the open edge. Adapter is the sole hard-prune owner and clears chunk dedupe for dropped ranges.

**Return-to-live catch-up:** when camera returns to k=0 after History, fill missing ring sub-segments history-style (high budget, most-incomplete window first) with **Status = Catching up**, then tip refresh/seeds.

**Cache / retention:** keep loaded blocks when the view slides away (draw cull only). Soft/hard RAM warnings; last-resort oldest-node prune near ~2 GB. **Segment disk cache (design):** verified BFS windows → `%LOCALAPPDATA%` per domain; bootstrap load before network — [segment-disk-cache.md](../segment-disk-cache.md).

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
| **In progress** | Hole-priority subsegments + variable patches | Disk per-chunk presence; dep-critical hole queue before newest-first bulk; variable interval spans |

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
