# mod · domain

One **subfolder per test**. Optional `expected/` only when the case needs committed reference files; runtime dumps use gitignored `out/`.

| Test id | `expected/` | Notes |
|---------|-------------|--------|
| `add_idempotent_and_confirm` | — | add + mark_confirmed |
| `remove_erases_confirmed` | — | remove clears confirm |
| `confirm_readmit_after_remove` | — | re-admit re-marks |
| `detail_slim_pin` | — | AlphDetailStore slim/pin |
| `detail_via_scene` | — | slim via scene store |
| `graph_lane` | — | chainFrom/To → lane |
| `prune_protects_frontier` | — | prune keeps tip |

Shared fixtures: `_shared/make_block.hpp` (not a test).

## Add a test

1. `mkdir vnv/mod/tests/domain/<id>`
2. Write `<id>/test.cpp` with `void test_<id>(VnvStats&)`
3. If needed: `<id>/expected/...` (committed)
4. Register in `main.cpp` + `sln/mod_domain.vcxproj` ClCompile
5. `.\scripts\run_vnv.ps1 -Mod`
