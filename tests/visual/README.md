# Graphics visual regression (screenshot compare)

Deterministic **FakeChain** render + PNG capture + pixel compare against goldens.

## Quick start

From **repo root** (so paths resolve):

```powershell
# First time / after intentional visual change — write golden:
.\scripts\run_visual_tests.ps1 -UpdateGoldens

# Regression check:
.\scripts\run_visual_tests.ps1
```

Optional:

```powershell
.\scripts\run_visual_tests.ps1 -Case fake_overview -Configuration Release
.\scripts\run_visual_tests.ps1 -SkipBuild   # reuse existing exe
```

## Layout

| Path | Role |
|------|------|
| `goldens/<case>.png` | Committed baseline |
| `out/<case>/actual.png` | Last capture (gitignored) |
| `out/<case>/diff.png` | Heatmap on failure |
| `out/<case>/report.txt` | Bad pixel stats |
| `cases/<case>.json` | Case metadata (docs; harness may hard-code V1) |
| `compare_images.ps1` | Pixel threshold compare |
| `graphics_visual_tests.cpp` | Capture harness |

## Case: `fake_overview` (V1)

- Window client **1280×720**
- Domain **Debug** / FakeChain (no HTTP)
- No app ImGui overlay (`set_ui_overlay(nullptr)`)
- Fixed camera scroll / look
- Warmup ~4.5s then BitBlt screenshot

## Compare defaults

| Param | Value |
|-------|--------|
| Per-channel max delta | 8 / 255 |
| Max bad pixel fraction | 0.2% |

## Golden machine

Baselines are **machine- and GPU-sensitive** (BitBlt + driver). Update goldens on the workstation you treat as authoritative; expect flakes across different GPUs until Phase 2 GPU readback.

Recommended: same `Configuration` (Debug or Release) for update and compare. Document driver if the whole suite suddenly fails after an update.

## Phases (see project plan)

1. **V1** — this harness (BitBlt + compare)  
2. **V2** — tighter determinism (anim freeze, etc.)  
3. **V3** — Vulkan readback capture  
4. **V4** — more cases / optional GPU CI  

## Not covered here

- Domain logic → `blockviz_tests`  
- Vulkan VUIDs → `.grok/skills/vulkan-validator`  
