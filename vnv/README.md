# Verification & Validation (VnV)

Automated checks for **alephium_blockviz**, separate from the product app solution.

| Category | Path | GPU? | Purpose |
|----------|------|------|---------|
| **mod** | `vnv/mod/tests/<area>/<test_id>/` | No | Module logic (one subfolder per test; optional `expected/`) |
| **int** | `vnv/int/tests/` | Yes | Integration (engine + systems, visual goldens) |
| **bench** | `vnv/bench/tests/` | Yes | Performance baselines via frame profiler (`-Bench`, opt-in) |

## Solutions

| Solution | Role |
|----------|------|
| `sln/alephium_visualizer.sln` | **Product only** (app + shared libs) |
| `sln/blockviz_vnv.sln` | **VnV** (shared libs + mod/int projects) |

Shared libraries (`graphics`, `network`, `blockviz_engine`) have **one** `.vcxproj` each. Both solutions reference the same files.

## Sync solutions (when shared libs change)

Manifests under `vnv/manifest/` are the source of truth:

- `libraries.json` — shared static libs  
- `product_apps.json` — product executables  
- `vnv_projects.json` — mod / int / bench test projects  

```powershell
# After adding/removing a library or VnV project:
.\scripts\sync_solutions.ps1

# CI / pre-commit: fail if committed slns drift from manifests
.\scripts\sync_solutions.ps1 -CheckOnly
```

### New shared library checklist

1. Copy `sln/_template_system.vcxproj.example` → `sln/<name>.vcxproj`  
2. Implement `src/<name>/`  
3. Register in `vnv/manifest/libraries.json` (id, path, guid, depends_on, in_product_sln / in_vnv_sln)  
4. `.\scripts\sync_solutions.ps1`  
5. Add `ProjectReference` from engine/app consumers if needed  
6. `.\scripts\run_vnv.ps1 -All`  
7. Commit lib + manifests + both `.sln` files  

## Run VnV

### Windows (MSVC)

```powershell
# Fast (mod only) — default
.\scripts\run_vnv.ps1

# Full gate before further features / graphics push
.\scripts\run_vnv.ps1 -All

.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -Int -UpdateGoldens

# Performance (opt-in; not in -All)
.\scripts\run_vnv.ps1 -Bench
.\scripts\run_vnv.ps1 -Bench -UpdateBaselines
.\scripts\run_vnv.ps1 -Bench -Configuration Release
```

### Linux (CMake)

```bash
# Fast (mod only) — default; builds mod_domain + mod_network
./scripts/run_vnv.sh

./scripts/run_vnv.sh --skip-build
./scripts/run_vnv.sh --ctest
```

`int` / `bench` are still Windows-only (`run_vnv.ps1 -Int` / `-Bench`). See [docs/linux.md](../docs/linux.md).

Working directory must be **repo root**.

## Visual goldens (int)

See `vnv/int/tests/visual/README.md`.

## Bench (frame profiler)

See `vnv/bench/README.md` — FakeChain + `FrameTimingSnapshot` median/p95 baselines.
