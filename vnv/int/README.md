# VnV · int (integration tests)

Multi-system tests; may use GPU / screenshots.

| Project | Sources | Binary |
|---------|---------|--------|
| `int_visual` | `vnv/int/tests/visual/` | `build/int_visual/<Config>/int_visual.exe` |

```powershell
.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -All
.\scripts\run_vnv.ps1 -Int -UpdateGoldens
```

Details: `vnv/int/tests/visual/README.md`.
