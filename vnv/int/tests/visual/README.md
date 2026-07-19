# int · visual regression

Deterministic **FakeChain** render + PNG capture + pixel compare against goldens.

## Commands

Prefer the VnV runner:

```powershell
.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -Int -UpdateGoldens
.\scripts\run_vnv.ps1 -All
```

Legacy wrapper: `.\scripts\run_visual_tests.ps1`

## Layout

| Path | Role |
|------|------|
| `goldens/<case>.png` | Committed baseline |
| `out/<case>/actual.png` | Last capture (gitignored) |
| `out/<case>/diff.png` | Heatmap on failure |
| `compare_images.ps1` | Pixel threshold compare |
| `graphics_visual_tests.cpp` | Capture harness → `int_visual.exe` |

## Case: `fake_overview`

- 1280×720, Debug FakeChain, fixed camera, no app overlay  
- Warmup ~4.5s then BitBlt screenshot  

## Compare defaults

Per-channel max delta **8**; max bad fraction **0.2%**.

Goldens are GPU/machine-sensitive until GPU readback (later phase).
