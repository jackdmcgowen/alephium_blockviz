# Planning notes (integration/planning/01)

Living product + engineering plans for **alephium_blockviz**. Browser view: [index.html](index.html).

**Working branch for implementation:** `integration/docs_site_and_motion/01`  
**Do not land features on `main` until squash/release.**

---

## Branch policy

| Branch | Use |
|--------|-----|
| `main` | Releases only; tags `app-v*` / `engine-v*` (see `AGENTS.md`) |
| `integration/planning/01` | Plans, critique, motion/docs specs (this folder) |
| `integration/docs_site_and_motion/01` | HTML parity + motion/art code |

---

## Brutal honesty (blockchain enthusiast)

### Strong

- Real multi-shard BlockFlow (16 lanes, tips, deps) — uncommon and interesting  
- Live mainnet + history fill + disk cache — explorer energy, not a toy  
- Semantic colors (tip / unconfirmed / incomplete / select) once learned  
- Polar + timeline Z layout is a **distinctive identity** — keep it  

### Weak / wanted

| Gap | Ask |
|-----|-----|
| Cold start | 30s of rails + arrow soup; need 3-step “what is this?” |
| Hierarchy | Arrows beat blocks; default fewer edges, power-user “all deps” |
| Time | Mainnet clump spacing looks random without ticks/density cues |
| Trust | “Why green?” one click (height, bag, explorer) |
| Perf | ~25 fps @ 12k blocks feels unfinished; Prepare is the bottleneck |
| Art | Flat cubes + neon; motion + materials will outrank layout redesign |
| Share | Clean screenshot mode (hide rails) |

**Layout is good. Next investment: motion, materials, information hierarchy — not a new layout.**

---

## Motion language (no over-tween)

Principles: one hero motion per event; ease-out / ease-in-out; overshoot only on pop-in; compute only on **drawn ring**.

| Event | Motion | Easing |
|-------|--------|--------|
| Block admit (visible) | Scale 0 → ~1.08 → 1.0 | ease-out-back |
| Tip advance | Scale pulse 1 → 1.12 → 1 | ease-in-out |
| Selection | Brief gold pop + Sobel | ease-out |
| Network wave | Staggered bump along Z (“the wave”) | ease-in-out, **rare** |
| Arrow grow | Path length `u` | ease-out cubic (not linear) |
| Shape | Light squash/stretch on pop | ease-out-back |
| Death | Scale down + alpha | ease-in |

**Avoid:** idle breathing, continuous orbit, full-graph animation.

Style tokens: `resource/style_blockflow.json` durations/amplitudes.

---

## Docs HTML parity

Rule: **every `docs/**/*.md` has a sibling `.html`** (same nav chrome).

Markdown currently missing HTML:

- `ROADMAP.md`, `platform.md`, `linux.md`, `perf-bottlenecks.md`, `build-performance.md`
- `segment-disk-cache.md`, `brand/alephium_palette.md`, `layers/README.md`
- Archives: `blockflow-confirmed-tips-design.md`, `graphics-modularization-design.md`

Hub (`docs/index.html`): Guides · Story · Layers · Platform · Perf · Brand · Archives · Planning.

Scripts (on motion branch):

- `scripts/md_to_docs_html.py` — MD source of truth → HTML body + nav  
- `scripts/check_docs_html_parity.py` — fail if MD without HTML  

---

## PR sequence (`integration/docs_site_and_motion/01`)

| PR | Deliverable |
|----|-------------|
| **PR1** | HTML parity + hub + check script |
| **PR2** | `motion_easing.hpp` + block pop-in overshoot |
| **PR3** | Wave / shuffle bump + style tokens |
| **PR4** | Arrow ease-out + death scale; legend docs |

Then squash/merge to `main` when ready; tag per AGENTS.md.

---

## Related

- [docs hub](../index.html)  
- [layers](../layers/index.html)  
- [perf bottlenecks](../perf-bottlenecks.md)  
- [brand palette](../brand/alephium_palette.md)  
