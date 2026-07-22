# VnV · integration tests

**Tier:** **Integration** — two or more **modules** linked together **without** a full product shell (no full `GraphicsSystem` present path).  
**Taxonomy:** [../TESTING.md](../TESTING.md).

## vs unit vs system

| | Unit | Integration | System |
|--|------|-------------|--------|
| Modules | One `src/` area | Two or more | Multiple **systems** via engine |
| Example | `BlockScene` only | network + domain, fake HTTP | FakeChain + GPU goldens |
| Path today | `vnv/mod/tests/<area>/` | **`vnv/integration/tests/`** | `vnv/int/`, `vnv/bench/` |

## Layout (when adding suites)

```text
vnv/integration/tests/<composition>/
  main.cpp                 # suite driver (when binary exists)
  _shared/
  <test_id>/
    test.cpp
  README.md
```

`<composition>` names the modules, e.g. `domain_network`, `engine_network`.

## Status

Scaffold only — **no harness binary yet**. First suite should:

1. Add sources under `tests/<composition>/`.  
2. Register a project in `vnv/manifest/vnv_projects.json`.  
3. Run `.\scripts\sync_solutions.ps1`.  
4. Document linked libraries in the suite README.

Do **not** put multi-system GPU harnesses here; those are [system · functional](../int/README.md).
