# Planning notes

Living product notes for **alephium_blockviz**. Backlog: **[ROADMAP](../ROADMAP.md)** (single source).

---

## Branch policy

| Branch | Use |
|--------|-----|
| `main` | Releases only; tags `app-v*` / `engine-v*` ([AGENTS.md](../../AGENTS.md)) |
| `integration/planning/01` | Current integration tip (docs + motion squash) — **not** main until release |

Feature work → new `integration/<theme>/01` or this tip. Never land product features straight on `main`.

---

## Brutal honesty (still open)

| Gap | Ask |
|-----|-----|
| Perf | Prepare bottleneck @ dense N |
| Cold start | 3-step “what is this?” |
| Hierarchy | Fewer default arrows |
| Trust | “Why green?” one click |
| Art | Materials after motion |
| Share | Screenshot mode (hide rails) |

**Layout is good.** Next: Prepare perf, hierarchy, cold start — not a new layout.

---

## Motion (shipped)

Drawn ring only. Tokens: `resource/style_blockflow.json`. Details: [legend](../user-guide-legend.html).

| Event | Motion |
|-------|--------|
| Block admit | Scale pop ease-out-back (N-cap) |
| History fill | Rare Y-wave (cooldown) |
| Selection/hover arrows | Grow ease-out cubic |
| Death | Scale + α ease-in |

Code: `ring_motion.hpp`, `motion_easing.hpp`.

---

## Related

- [ROADMAP](../ROADMAP.md) · [layers](../layers/index.html) · [docs hub](../index.html)
