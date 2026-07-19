# Agent instructions (alephium_blockviz)

## Required: version tags whenever `main` is pushed

**Anytime `main` is updated on the remote** (merge, fast-forward, direct push, or force-with-lease), you **must** create and push annotated release tags that match the identities in source **before considering the main push complete**.

### Tag pair (same commit as `main` tip)

| Tag | Source of truth | Message pattern (annotated) |
|-----|-----------------|-----------------------------|
| `app-vMAJOR.MINOR.PATCH` | `src/app/app_identity.hpp` (`app_identity::kVersion*`) | `Alephium BlockFlow X.Y.Z — host application version` |
| `engine-vMAJOR.MINOR.PATCH` | `src/engine/engine_identity.hpp` (`blockviz_engine::kVersion*`) | `BlockvizEngine X.Y.Z — <short surface summary>` |

Historical examples:

- `app-v0.1.0` / `engine-v0.2.0` on modularization landing  
- `app-v0.3.0` / `engine-v0.4.0` on BlockFlow viz + Network panel release  

### Procedure (do not skip)

1. Confirm `main` tip and that `app_identity` / `engine_identity` versions are what you intend to ship.  
2. If versions were not bumped for a meaningful release, **bump them in a commit on `main` first** (or as part of the merge), then tag.  
3. Create **annotated** tags on the **exact** `main` commit:

```powershell
git checkout main
git pull origin main
# read versions from app_identity.hpp / engine_identity.hpp
git tag -a app-vX.Y.Z -m "Alephium BlockFlow X.Y.Z — host application version"
git tag -a engine-vA.B.C -m "BlockvizEngine A.B.C — <one-line surface summary>"
git push origin app-vX.Y.Z engine-vA.B.C
```

4. Verify remote tags exist (`git ls-remote --tags origin "app-v*" "engine-v*"`).  
5. Do **not** retag an existing `app-v*` / `engine-v*` name on a different commit without an explicit user override.  
6. Pre-squash / branch backup tags (`pre-squash/…`, `pre-main-squash/…`) are optional and **do not** replace the app/engine version tags.

### Failure condition

If `origin/main` moved and the matching `app-v*` / `engine-v*` tags for the **current** identity versions are missing on that commit, the main push workflow is **incomplete** — create and push the tags before ending the session.

## Related gates

- **VnV (verification & validation):** see `vnv/README.md`.
  - After adding/removing a **shared library** or VnV project: edit `vnv/manifest/*.json`, then `.\scripts\sync_solutions.ps1` (updates product + VnV `.sln` files).
  - Domain/network logic: `.\scripts\run_vnv.ps1` (mod).
  - Graphics/presentation or before further features: `.\scripts\run_vnv.ps1 -All`.
  - Drift check: `.\scripts\sync_solutions.ps1 -CheckOnly`.
- Vulkan validation: follow `.grok/skills/vulkan-validator/SKILL.md` before committing/pushing graphics changes.
- Build speed / PCH / IWYU / adding systems: see `docs/build-performance.md`. Use `scripts/bench_build.ps1` before claiming compile-time improvements.

## Build hygiene (PCH / includes)

- Product `.cpp` files start with `#include "<area>/pch.h"` (`graphics/`, `network/`, `engine/`, `app/`).
- Do not put frequently edited product headers into PCH files.
- Prefer forward declarations in headers; keep `gpu_pub_lib.h` Vulkan-free.
- New static-lib systems: copy `sln/_template_system.vcxproj.example` and add a project-local PCH.
