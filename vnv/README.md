# Verification & Validation (VnV)

Automated checks for **alephium_blockviz**, separate from the product app solution.

**Test taxonomy and how to add tests:** **[TESTING.md](TESTING.md)** (authority).

> **mod** in paths/CLI = **unit / module tests**, not product modularization.

## Tiers

| Tier | Disk (today) | GPU? | Purpose |
|------|--------------|------|---------|
| **Unit** | `vnv/mod/tests/<src_area>/` | No | One `src/` area in isolation (1:1 map) |
| **Integration** | `vnv/integration/tests/` | Prefer no | Multiple modules, no full product shell |
| **System · functional** | `vnv/int/tests/` | Yes | Multi-system scenarios (e.g. visual goldens) |
| **System · bench** | `vnv/bench/` | Yes | Perf baselines (opt-in; sub-category of system) |

Long-term path names (`unit/`, `system/functional/`, …) are documented in TESTING.md; physical rename is optional later.

## Solutions

| Solution | Role |
|----------|------|
| `sln/alephium_visualizer.sln` | **Product only** (app + shared libs) |
| `sln/blockviz_vnv.sln` | **VnV** (shared libs + unit/system harnesses) |

Shared libraries (`graphics`, `network`, `blockviz_engine`) have **one** `.vcxproj` each. Both solutions reference the same files.

## Sync solutions (when shared libs change)

Manifests under `vnv/manifest/` are the source of truth:

- `libraries.json` — shared static libs  
- `product_apps.json` — product executables  
- `vnv_projects.json` — unit / system / bench test projects  

```powershell
# After adding/removing a library or VnV project:
.\scripts\sync_solutions.ps1

# CI / pre-commit: fail if committed slns drift from manifests
.\scripts\sync_solutions.ps1 -CheckOnly
```

### New shared library checklist

1. Copy `sln/_template_system.vcxproj.example` → `sln/<name>.vcxproj`  
2. Implement `src/<name>/`  
3. Register in `vnv/manifest/libraries.json`  
4. `.\scripts\sync_solutions.ps1`  
5. Add `ProjectReference` from engine/app consumers if needed  
6. `.\scripts\run_vnv.ps1 -All`  
7. Commit lib + manifests + both `.sln` files  

Full test authoring rules: [TESTING.md](TESTING.md).

## Run VnV

### Windows (MSVC)

```powershell
# Fast (unit only) — default
.\scripts\run_vnv.ps1

# Full gate: unit + system functional (bench stays opt-in)
.\scripts\run_vnv.ps1 -All

.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -Int -UpdateGoldens

# Performance (system · bench; not in -All)
.\scripts\run_vnv.ps1 -Bench
.\scripts\run_vnv.ps1 -Bench -UpdateBaselines
.\scripts\run_vnv.ps1 -Bench -Configuration Release
```

### Linux (CMake)

```bash
./scripts/run_vnv.sh                 # unit (default)
./scripts/run_vnv.sh --skip-build
./scripts/run_vnv.sh --ctest
./scripts/run_vnv.sh --int           # system functional (DISPLAY + GPU)
./scripts/run_vnv.sh --int --headless
./scripts/run_vnv.sh --bench         # system · bench (opt-in)
./scripts/run_vnv.sh --all           # unit + system functional
```

CI: `.github/workflows/linux-ci.yml`. See [docs/linux.md](../docs/linux.md).

Working directory must be **repo root**.

## Category entry points

| Path | Doc |
|------|-----|
| Unit layout | [mod/README.md](mod/README.md) |
| System · functional | [int/README.md](int/README.md) · [int/tests/visual/](int/tests/visual/README.md) |
| System · bench | [bench/README.md](bench/README.md) |
| Integration (future suites) | [integration/README.md](integration/README.md) |
