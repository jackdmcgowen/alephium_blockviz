# Multi-GPU bench matrix (Linux farm)

**Hardware:** 7× NVIDIA (5× 3070 Ti, 3080, 3090) on **PCIe ×1 risers** + lavapipe.  
**Present:** `xvfb-run` windowed (NVIDIA has no headless surface).  
**Isolation:** `--device <vulkan-uuid>` (not CUDA_VISIBLE_DEVICES alone).

| Artifact | Description |
|----------|-------------|
| [matrix_table.html](matrix_table.html) | Compact GPU × case table |
| [matrix_run.html](matrix_run.html) | Full VnV HTML report |
| [matrix_run.json](matrix_run.json) | Machine-readable cells |
| `out/<tag>/<case>/actual.json` | Per-GPU actuals |

Soft confidence is **advisory** on this farm (desktop budgets + Xvfb present-sync). Mass cases (~2061 blocks) pass conf with GPU ≪ 1 ms on discrete cards.
