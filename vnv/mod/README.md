# VnV · unit tests (module isolation)

**Tier:** **Unit** — one `src/` area in isolation.  
**Taxonomy:** [../TESTING.md](../TESTING.md).

> Path is still `vnv/mod/` for historical reasons. **mod ≠ modularization.** Prefer saying **unit tests** in prose and PRs.

CPU-only; no window/swapchain.

## Map to `src/`

| Suite path | Product code |
|------------|--------------|
| `tests/domain/` | `src/domain/` |
| `tests/network/` | `src/network/` |
| `tests/graphics/` | `src/graphics/` (add here when pure unit coverage grows) |
| `tests/engine/` | `src/engine/` |
| `tests/app/` | `src/app/` pure helpers only |
| `tests/common/` | `src/common/` |

## Layout rule (required)

```text
vnv/mod/tests/<src_area>/<test_id>/test.cpp
vnv/mod/tests/<src_area>/<test_id>/expected/   # optional
vnv/mod/tests/<src_area>/<test_id>/out/        # gitignored dumps
vnv/mod/tests/<src_area>/_shared/              # fixtures only
vnv/mod/tests/<src_area>/main.cpp              # suite driver
```

- **One test = one subfolder** under the area.  
- Primary file is always **`test.cpp`** (MSVC unique `ObjectFileName`).  
- Create **`expected/`** only when the test needs reference files.  
- Do not drop multi-test `.cpp` files at the area root.

| Project | Sources | Binary |
|---------|---------|--------|
| `mod_domain` | `vnv/mod/tests/domain/` | `build/mod_domain/<Config>/mod_domain.exe` |
| `mod_network` | `vnv/mod/tests/network/` | `build/mod_network/<Config>/mod_network.exe` |

```powershell
.\scripts\run_vnv.ps1          # default = all unit suites
.\scripts\run_vnv.ps1 -Mod
```

How to add a test: [TESTING.md § Unit](../TESTING.md#tier-a--unit-tests-detail).
