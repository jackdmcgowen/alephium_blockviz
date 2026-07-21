# Segment disk cache (design)

**Status:** **Segment hierarchy cache (v3)** — `G_<id>/` directory with multi-chunk gzip entries.  
**Goal:** Bootstrap recent sessions; complete historical G packs replace network body loads; live edge stays network-first.

### Unit of work

| Key | `G_seg` genesis index → 60s chunk files |
|-----|----------------------------------------|
| Save | Bucket blocks by 60s → `segments/G_<id>/c_<from>.json.gz` + `meta.json` |
| Load | Stream present `c_*` files (partial G OK); seed only those grid keys; live open tip never disk-complete |
| Network | `try_fill_window_from_disk_(k)` before interval GET; live tip always `force_newest` |

### Debug checklist

1. **Folder appears on domain start** (empty manifest) under  
   `%LOCALAPPDATA%\\AlephiumBlockViz\\cache\\mainnet\\`  
2. **Network panel → Disk cache**: path, last event, on-disk counts.  
3. **`cache.log`** in that folder: save/load/fill/flush lines.  
4. Warm save after ~16+ blocks in a G (Steady); complete when all 60s chunks present.  
5. **Close app / switch domain** → `flush` writes remaining windows.  
6. Restart: boot event + `loaded G=… from cache (skip net)` for complete G.

## Intent

| When | Behavior |
|------|----------|
| Segment becomes **fully verifiable** via adapter **BFS walk** | Persist segment + block payloads to disk (per domain) |
| **Startup** or **domain reload** | Load verified cached segments first (graph + detail + interval dedupe), then network for live/holes/uncached |
| Live tip (`k=0` open window) | Prefer network; **never** treat open live G as interval-complete from disk. Bootstrap may **paint** live-G blocks (bag-only confirmed), but must force-refresh the **topmost 60s subsegment** before IdentifyTips. Sequential frontiers are cleared after disk boot so `H_c` resolves from live data. |

This is a **bootstrap** layer, not a full archive and not a substitute for main-chain API trust without optional re-verify.

## Terminology

| Term | Meaning |
|------|---------|
| **G_seg** | Genesis-aligned segment index (`G_live − k`) |
| **Window** | `[from_ms, to_ms)` = `genesis + G_seg·window` … + `ALPH_LOOKBACK_WINDOW_SECONDS` |
| **Verified** | Parallel BFS confirm (`N=2G−1`) has completed for that window’s frontier/bag such that blocks in the window are admitted and confirmation marks are stable enough to skip re-poll of the interval body |
| **Cacheable** | Historical (closed) window preferred; live open edge stays network-first until the window freezes (`k≥1`) |

## Disk layout (schema **v3** — G directory + multi-chunk entries)

```text
%LOCALAPPDATA%/AlephiumBlockViz/cache/<domain_id>/
  manifest.json                 # schema 3 index (RAM-cached after open)
  cache.log                     # save/load/fill/flush events
  segments/G_<id>/
    meta.json                   # from_ms, to_ms, complete, block_count, chunk_count
    c_<chunk_from_ms>.json.gz   # one entry per 60s subsegment: { blocks: [ … ] }
```

**Removed (v1):** per-block `blocks/<hh>/<hash>.json.gz` (wiped on domain open).  
**Migrated (v2):** flat `segments/G_<id>.json.gz` → v3 hierarchy on domain open.

| Field (manifest segment) | Role |
|--------------------------|------|
| `g_seg` | Genesis segment id |
| `from_ms` / `to_ms` | Window bounds |
| `complete` | Safe to skip network body for this G (historical only) |
| `block_count` | Blocks across chunk entries |
| `schema_version` | **3** (v2 packs auto-migrated) |

**Domain ids:** `mainnet` / `testnet` / `debug` (match `NetworkDomain`).

## Caps (reasonable space/time)

| Cap | Suggested v1 |
|-----|----------------|
| Max verified segments | 32–64 (LRU by recency toward live) |
| Max disk | e.g. 512 MB–1 GB hard stop |
| Startup load | Most recent **15** verified segments (`ALPH_LOAD_RING_SEGMENTS`); **chunked admit** to avoid frame hitch |
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
     - **Historical G (`G < G_live`)**: mark interval keys fetched; skip network body
     - **Live G (`G == G_live`)**: paint only; leave chunk load-once empty; `want_newest_refresh`
     - Restore confirmation as **bag-only** (no sequential `H_c`); clear frontiers after boot
5. Then start normal poll:
     - Force topmost live 60s subsegment from network (even pre-Steady)
     - Live tip / k=0 always network for open edge
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
| **P3** | **Done:** 512 MiB cap, orphan GC, Network HUD line, save k=0 warm + k≥1, load diagnostics |

## Open decisions

1. Exact BFS completeness predicate (tips only vs all heights in window).  
2. Whether to store confirmation flags on disk or re-run BFS after admit.  
3. Startup N vs disk N (load fewer than retained).  
4. Compression library already available? (zlib via curl/vcpkg `z` DLL is present — prefer that).  

## Success metrics

- Second launch of same domain shows cubes for recent history **before** first interval HTTP completes.  
- Disk stays under cap.  
- Live tip remains correct and not stuck on stale cache.
