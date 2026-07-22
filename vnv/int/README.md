# VnV · system tests (functional)

**Tier:** **System · functional** — multiple systems via engine / host-like path (correctness, visual goldens).  
**Taxonomy:** [../TESTING.md](../TESTING.md).

> Path is still `vnv/int/` (historical “integration”). These are **system** tests, not multi-module integration without a device. Multi-module-only suites belong under [../integration/](../integration/README.md).

Usually **GPU**; may use headless surface.

| Project | Sources | Binary |
|---------|---------|--------|
| `int_visual` | `vnv/int/tests/visual/` | `build/int_visual/<Config>/int_visual.exe` |

```powershell
.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -All
.\scripts\run_vnv.ps1 -Int -UpdateGoldens
```

Cases and goldens: [tests/visual/README.md](tests/visual/README.md).

**Related:** performance budgets are **system · bench** — [../bench/README.md](../bench/README.md).
