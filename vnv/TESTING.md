# VnV testing guidelines

How to **add and place** automated tests for **alephium_blockviz**.

**Authority:** this document. Category READMEs (`mod/`, `int/`, `bench/`) and [vnv/README.md](README.md) summarize runners and solutions; they defer taxonomy here.

> **Naming:** **mod** in paths and CLI means **unit / module tests**, not product “modularization.” Prefer the words **unit**, **integration**, and **system** in new prose.

---

## Purpose

| | Product app | VnV |
|--|-------------|-----|
| Solution | `sln/alephium_visualizer.sln` | `sln/blockviz_vnv.sln` (+ CMake VnV targets) |
| Goal | Ship the visualizer | Prove modules and stacks still behave |
| Working directory | Repo root | Repo root |

Shared libraries (`graphics`, `network`, `blockviz_engine`) have **one** `.vcxproj` each; both solutions reference them. Manifests: `vnv/manifest/`. Sync: `.\scripts\sync_solutions.ps1`.

---

## Tiers at a glance

| Tier | Doc name | What it proves | GPU? | Maps to `src/` |
|------|----------|----------------|------|----------------|
| **A** | **Unit** (module) | One layer/library in isolation | No (default) | **1:1** area name |
| **B** | **Integration** | Two or more **modules** wired together, no full product shell | Prefer no | Composition name (not 1:1) |
| **C** | **System · functional** | Multiple **systems** via engine / host-like path | Usually yes | Scenario suites |
| **C′** | **System · bench** | Same system stack; **timing** budgets | Yes | Scenario + baselines |

**Bench is a sub-category of system tests**, not a peer world of its own. Pass criteria differ (pixels / behavior vs median·p95).

### Decision tree

```text
Need a real window + swapchain + GraphicsSystem + NetworkSystem + IEngine?
  YES → System · functional  (or System · bench if the goal is perf)
  NO
    Need two or more src modules linked (e.g. network + domain)?
      YES → Integration
      NO  → Unit (single src area)
```

| If you are testing… | Choose |
|---------------------|--------|
| `BlockScene::prune`, graph lane math | **Unit · domain** |
| `HttpIoPool` dedupe with fake transport | **Unit · network** |
| Adapter dual-write into `BlockScene` without rendering | **Integration · domain_network** |
| FakeChain + GPU screenshot goldens | **System · functional** (`visual`) |
| Frame profiler median/p95 | **System · bench** |

---

## Map to `src/`

```text
src/                              vnv/ (logical layout)
  domain/              ◄────────  unit/tests/domain/       [today: mod/tests/domain/]
  network/             ◄────────  unit/tests/network/      [today: mod/tests/network/]
  graphics/            ◄────────  unit/tests/graphics/     (grow here)
  engine/              ◄────────  unit/tests/engine/
  app/                 ◄────────  unit/tests/app/          (pure logic only)
  common/              ◄────────  unit/tests/common/

  multi-module         ◄────────  integration/tests/<a>_<b>/...

  full stack           ◄────────  system/functional/...    [today: int/]
                              └─  system/bench/...         [today: bench/]
```

### Unit: 1:1 with top-level `src` areas

| Rule | Detail |
|------|--------|
| Area folder = `src/<area>` | e.g. `domain` tests live under `…/tests/domain/` |
| Area = **library / layer**, not every subdirectory | Code under `src/network/alephium/` still tests as **network** |
| Optional deeper folders | Only if an area has many tests (e.g. `graphics/frame/…`); otherwise flat `test_id` under the area |
| **App** unit tests | Pure helpers only (`user_prefs` parse, chrome layout math)—not full overlay + GPU |
| **Not unit** | Anything requiring `IEngine` + full systems, live Vulkan device, or poll+render |

### Integration: composition paths

Path names the **set of modules**, not a single `src` folder:

```text
integration/tests/domain_network/<test_id>/
integration/tests/engine_network/<test_id>/
```

Prefer alphabetical or stable short phrases (`domain_network`). One harness project per composition when suites grow.

### System: scenario suites

Not 1:1 with `src/`. Name the **scenario** (`visual`, `smoke_boot`, `fake_steady_frame`).

---

## Legacy paths (until a migration PR)

Physical directories are **not** renamed yet. Treat these as aliases:

| Logical tier | Path on disk today | Runner flag |
|--------------|--------------------|-------------|
| Unit | `vnv/mod/` | `-Mod` / default / `--mod` |
| Integration | `vnv/integration/` (stub; grow here) | (none yet) |
| System · functional | `vnv/int/` | `-Int` / `--int` |
| System · bench | `vnv/bench/` | `-Bench` / `--bench` |

Long-term preferred names: `vnv/unit/`, `vnv/system/functional/`, `vnv/system/bench/`. See ROADMAP for optional rename work.

---

## Tier A — Unit tests (detail)

### Layout (required)

```text
vnv/mod/tests/<src_area>/          # unit · alias path
  main.cpp
  _shared/                         # fixtures only (not a test)
  <test_id>/
    test.cpp                       # always this name (MSVC unique objs)
    expected/                      # optional, committed
    out/                           # optional, gitignored
  README.md
```

- **One test = one subfolder.** Do not drop multi-test `.cpp` files at the area root.
- Use `vnv/framework/expect.hpp` for assertions.
- No window, swapchain, or validation layers.

### Existing suites

| Project | Sources | Binary |
|---------|---------|--------|
| `mod_domain` | `vnv/mod/tests/domain/` | `build/mod_domain/<Config>/mod_domain.exe` |
| `mod_network` | `vnv/mod/tests/network/` | `build/mod_network/<Config>/mod_network.exe` |

### How to add a unit test

1. Pick `<src_area>` matching `src/<area>/`.
2. Create `vnv/mod/tests/<src_area>/<test_id>/test.cpp`.
3. Register the file in the area’s suite `main.cpp` (or project sources) and **vcxproj** / CMake as required.
4. If new area or new VnV project: edit `vnv/manifest/vnv_projects.json`, then `.\scripts\sync_solutions.ps1`.
5. Run:

```powershell
.\scripts\run_vnv.ps1 -Mod
# Linux:
./scripts/run_vnv.sh
```

6. Keep tests deterministic: no wall-clock races, no live REST.

---

## Tier B — Integration tests (detail)

### When

- Adapter → `BlockScene` without graphics.
- Domain + network policy with fake HTTP.
- Two graphics subsystems without present (future).

### When not

- Full `create_engine` + `create_graphics_system` + `create_network_system` + present → **system**.

### Layout

```text
vnv/integration/
  README.md
  tests/
    <composition>/                 # e.g. domain_network
      main.cpp                     # when a suite binary exists
      _shared/
      <test_id>/
        test.cpp
```

### How to add an integration suite

1. Name composition (`domain_network`, …).
2. Add harness under `vnv/integration/tests/<composition>/`.
3. New vcxproj + `vnv_projects.json` entry (`category` may stay `mod` until runners grow an integration flag, or use a dedicated category when implemented).
4. `.\scripts\sync_solutions.ps1`.
5. Link **only** the libs you need; document them in the suite README.

*(Scaffold exists; first real suite is a follow-up PR.)*

---

## Tier C — System · functional (detail)

### What lives here today

| Project | Sources | Role |
|---------|---------|------|
| `int_visual` | `vnv/int/tests/visual/` | FakeChain + GPU PNG goldens |

Cases: `fake_overview`, `fake_side_cam`, `fake_selection_sobel` — see [int/tests/visual/README.md](int/tests/visual/README.md).

### How to add a visual case

1. Add case id in `graphics_visual_tests.cpp` (`kCases[]`) and document it.
2. Add to `vnv/manifest/vnv_projects.json` → `cases` array (and keep `run_vnv` loops in sync).
3. Capture goldens:

```powershell
.\scripts\run_vnv.ps1 -Int -UpdateGoldens
# Linux headless:
./scripts/run_vnv.sh --int --headless --update-goldens
```

4. Commit desktop goldens under `vnv/int/tests/visual/goldens/`; headless under `goldens/linux_headless/` when available.
5. Multi-platform policy: [int/tests/visual/goldens/README.md](int/tests/visual/goldens/README.md).

Harnesses may use GPU, headless surface, and full engine wiring. They are **system** tests even though the folder is still named `int/`.

---

## Tier C′ — System · bench (detail)

| | |
|--|--|
| Path | `vnv/bench/` (logical: system/bench) |
| Pass rule | Median (and tracked p95) ≤ baseline × (1 + tol); default tol **15%** |
| Default CI | **Opt-in** (`-Bench` / `--bench`); **not** part of `-All` free gate |
| Prefer | Release + validation off for baselines |

See [bench/README.md](bench/README.md).

### How to add a bench case

1. Add case to harness (`graphics_bench_tests.cpp` or successor).
2. Run `-Bench -UpdateBaselines` on a representative machine.
3. Commit `vnv/bench/baselines/<case>.json`.
4. Document non-goals: cross-machine absolute ms in free CI.

---

## Naming

| Kind | Convention | Example |
|------|------------|---------|
| `test_id` | `snake_case`, behavior-focused | `prune_protects_frontier` |
| Composition | modules joined | `domain_network` |
| Visual / bench case | `snake_case` scenario | `fake_side_cam`, `fake_steady_frame` |
| Avoid | “mod” in new titles; “test1”; unclear acronyms | |

---

## CI and runners

| Gate | What runs |
|------|-----------|
| Default / `-Mod` | Unit suites (`mod_domain`, `mod_network`, …) |
| `-Int` / `--int` | System · functional (visual cases) |
| `-All` | Unit + system functional (not bench) |
| `-Bench` | System · bench (opt-in) |
| Linux GHA | Unit + headless system visual (see workflow) |

```powershell
.\scripts\run_vnv.ps1              # unit
.\scripts\run_vnv.ps1 -All         # unit + system functional
.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -Bench
```

```bash
./scripts/run_vnv.sh
./scripts/run_vnv.sh --all
./scripts/run_vnv.sh --int --headless
./scripts/run_vnv.sh --bench
```

Hygiene (not tests, but required when touching app/build):

```text
python scripts/check_pch.py
python scripts/check_include_boundary.py
```

---

## Shared library or VnV project checklist

1. Implement `src/<name>/` and/or test sources under the correct tier path.  
2. Register library in `vnv/manifest/libraries.json` if new.  
3. Register VnV project in `vnv/manifest/vnv_projects.json`.  
4. `.\scripts\sync_solutions.ps1`  
5. `.\scripts\run_vnv.ps1 -All` (and `-Int` / `-Bench` if relevant)  
6. Commit sources + manifests + generated `.sln` files  

Template: `sln/_template_system.vcxproj.example`.

---

## Related

| Doc | Role |
|-----|------|
| [README.md](README.md) | Solutions, sync, quick run |
| [mod/README.md](mod/README.md) | Unit layout on disk |
| [int/README.md](int/README.md) | System · functional entry |
| [bench/README.md](bench/README.md) | System · bench entry |
| [integration/README.md](integration/README.md) | Integration tier stub |
| [docs/layers/README.md](../docs/layers/README.md) | Product layer map |
| [docs/platform.md](../docs/platform.md) | Dual-track / include isolation |
