# Segment disk cache (design)

**Status:** design on `feature/segment-disk-cache` — not implemented.  
**Goal:** Bootstrap recent sessions within bounded disk/time by caching **verified** timeline segments and replaying them before network fill.

## Intent

| When | Behavior |
|------|----------|
| Segment becomes **fully verifiable** via adapter **BFS walk** | Persist segment + block payloads to disk (per domain) |
| **Startup** or **domain reload** | Load verified cached segments first (graph + detail + interval dedupe), then network for live/holes/uncached |
| Live tip (`k=0` open window) | Prefer network; do not treat incomplete live window as verified cache |

This is a **bootstrap** layer, not a full archive and not a substitute for main-chain API trust without optional re-verify.

## Terminology

| Term | Meaning |
|------|---------|
| **G_seg** | Genesis-aligned segment index (`G_live − k`) |
| **Window** | `[from_ms, to_ms)` = `genesis + G_seg·window` … + `ALPH_LOOKBACK_WINDOW_SECONDS` |
| **Verified** | Parallel BFS confirm (`N=2G−1`) has completed for that window’s frontier/bag such that blocks in the window are admitted and confirmation marks are stable enough to skip re-poll of the interval body |
| **Cacheable** | Historical (closed) window preferred; live open edge stays network-first until the window freezes (`k≥1`) |

## Disk layout

```text
%LOCALAPPDATA%/AlephiumBlockViz/cache/<domain_id>/
  manifest.json                 # schema_version, segments[], caps, domain
  segments/G_<id>.json          # meta + ordered hash list
  blocks/<hh>/<hash>.json.gz    # AlphBlock detail JSON gzip
```

| Field (manifest segment) | Role |
|--------------------------|------|
| `g_seg` | Genesis segment id |
| `from_ms` / `to_ms` | Window bounds |
| `verified_at_unix` | When BFS/window policy accepted |
| `hashes[]` | Block hashes in segment (or file of hashes) |
| `schema_version` | Mismatch → ignore cache, re-fetch |

**Domain ids:** `mainnet` / `testnet` / `debug` (match `NetworkDomain`).

## Caps (reasonable space/time)

| Cap | Suggested v1 |
|-----|----------------|
| Max verified segments | 32–64 (LRU by recency toward live) |
| Max disk | e.g. 512 MB–1 GB hard stop |
| Startup load | Most recent N verified segments only (e.g. 8–16) for fast paint |
| Write | Async I/O on network/poll side — **never** render thread |

## Write path (when to cache)

```text
Trigger candidates (adapter network thread):
  A) BFS Trace / Steady maintenance: window G is closed (k≥1 for that G)
     AND interval chunks for G marked complete (load-once)
     AND BFS confirm marks cover the segment’s frontier tips
  B) Explicit “segment complete” after history ring fill + BFS pass

Then:
  1. Collect graph nodes with timestamp in [from_ms, to_ms)
  2. Serialize detail_store payloads (gzip)
  3. Write segments/G_<id>.json + update manifest
  4. Enforce LRU: drop oldest G_seg files over cap
```

**Do not** mark `verified` if:

- Only partial chunks loaded  
- BFS never finished for that frontier  
- Live open tip window still extending  

## Load path (startup / domain switch)

```text
1. Resolve cache root for domain
2. Read manifest; skip if schema mismatch
3. Pick recent verified segments (nearest G_live, up to startup N)
4. For each segment (oldest→newest or newest-first for UI feel):
     - Load block gzip → detail upsert + graph admit
     - Mark interval keys as fetched (history_slots_fetched_ / chunk keys)
     - Restore confirmation marks if stored (or light re-seed BFS)
5. Then start normal poll:
     - Live tip / k=0 always network
     - Ring holes only if not satisfied by cache
     - Older uncached segments on demand (user pages minimap)
```

**Priority after bootstrap:**

1. Live tip  
2. Camera ring uncached holes  
3. Older network segments only when paged  

## Integration map

| Component | Role |
|-----------|------|
| New `segment_disk_cache.*` (`network/alephium/` or `domain/`) | Manifest + gzip I/O + LRU |
| `AlephiumAdapter` | On verified-window event → enqueue snapshot; on domain start → load cache before Steady flood |
| Interval / `history_slots_fetched_` | Seed from cache so load-once skips re-GET |
| `BlockScene` / detail store | Bulk admit API if needed (batch under one lock) |
| `network_poller` | Domain switch: load cache then poll |
| HUD (optional) | “Cache: N segments / M MB” |

## Non-goals (v1)

- Full-chain archival  
- Shared cache across machines  
- UI file browser  
- Trust without optional re-verify (cache is speed, not consensus)  
- FakeChain disk (optional later; Debug can stay RAM-only)  

## Implementation PR split

| PR | Scope |
|----|--------|
| **P1** | Manifest schema + write closed verified segments + gzip blocks |
| **P2** | Startup/domain load priority + mark intervals fetched |
| **P3** | LRU/disk caps, HUD, optional background re-verify |

## Open decisions

1. Exact BFS completeness predicate (tips only vs all heights in window).  
2. Whether to store confirmation flags on disk or re-run BFS after admit.  
3. Startup N vs disk N (load fewer than retained).  
4. Compression library already available? (zlib via curl/vcpkg `z` DLL is present — prefer that).  

## Success metrics

- Second launch of same domain shows cubes for recent history **before** first interval HTTP completes.  
- Disk stays under cap.  
- Live tip remains correct and not stuck on stale cache.
