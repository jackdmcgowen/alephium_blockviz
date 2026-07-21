# Visual goldens — multi-platform policy

## Layout

| Path | When used |
|------|-----------|
| `fake_overview.png` | **Desktop / windowed** capture (historical Windows-style baseline) |
| `linux_headless/fake_overview.png` | **Headless** path (`--headless`, GPU readback, lavapipe/Mesa CI) |

## Rules

1. **Never** expect bit-identical pixels across ICDs or OS WSI (GDI blit vs GPU readback vs lavapipe).
2. **CI (headless)** compares against `linux_headless/` with **relaxed** thresholds (see `compare_images.py --profile headless`).
3. **Desktop** compares against the root golden with default tight thresholds (`--profile desktop`).
4. **Update goldens** only after intentional visual product changes:
   - Desktop: `./scripts/run_vnv.sh --int --update-goldens` (or PowerShell `-UpdateGoldens`)
   - Headless: `./scripts/run_vnv.sh --int --headless --update-goldens`
5. **Smoke without golden:** size 1280×720 + min file size is a minimum gate; full pixel compare is preferred when a matching golden exists.

## Capture notes

- Headless: `VK_EXT_headless_surface` + swapchain `TRANSFER_SRC` readback.
- Windowed Windows: GPU readback preferred; optional window blit only if `BLOCKVIZ_SCREENSHOT_WINDOW_BLIT=1`.
