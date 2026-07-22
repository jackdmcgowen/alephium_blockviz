# int · visual regression

Deterministic **FakeChain** render + PNG capture + pixel compare against goldens.

## Commands

Prefer the VnV runner:

```powershell
.\scripts\run_vnv.ps1 -Int
.\scripts\run_vnv.ps1 -Int -UpdateGoldens
.\scripts\run_vnv.ps1 -All
```

```bash
./scripts/run_vnv.sh --int
./scripts/run_vnv.sh --int --headless
./scripts/run_vnv.sh --int --headless --update-goldens   # → goldens/linux_headless/
./scripts/run_vnv.sh --all
# compare only:
python3 vnv/int/tests/visual/compare_images.py --profile headless \
  --expected vnv/int/tests/visual/goldens/linux_headless/fake_overview.png \
  --actual vnv/int/tests/visual/out/fake_overview/actual.png
```

Goldens policy: [goldens/README.md](goldens/README.md).

Legacy wrapper: `.\scripts\run_visual_tests.ps1`

## Layout

| Path | Role |
|------|------|
| `goldens/<case>.png` | Committed baseline |
| `out/<case>/actual.png` | Last capture (gitignored) |
| `out/<case>/diff.png` | Heatmap on failure |
| `compare_images.ps1` | Pixel threshold compare |
| `graphics_visual_tests.cpp` | Capture harness → `int_visual.exe` |

## Cases

| Case | What it checks |
|------|----------------|
| `fake_overview` | End-cam FakeChain overview (historical baseline) |
| `fake_side_cam` | Side view preset after settle |
| `fake_selection_sobel` | Select a confirmed tip (or any node); gold selection path + outlines |

Common setup: 1280×720, Debug FakeChain, no app overlay, warmup ~4.5s, then **GPU swapchain readback**.

List: `int_visual --list`

## Compare defaults

Per-channel max delta **8**; max bad fraction **0.2%**.

Goldens remain GPU/driver-sensitive; prefer headless profile for CI where configured (`goldens/linux_headless/`).
