# Agent instructions (alephium_blockviz)

## Submodules — do not modify

Vendored third-party trees live under **`lib/`** (git submodules):

| Path | Role |
|------|------|
| `lib/imgui` | Dear ImGui |
| `lib/vcpkg` | vcpkg tool + ports |

**Do not** edit, reformat, commit into, rebase, bump, or “fix” files inside `lib/imgui` or `lib/vcpkg` unless the user **explicitly** asks to update a submodule pin.

- Init only: `git submodule update --init --recursive`
- Deps install: root `install_deps.bat` / `install_deps.sh` (writes under `lib/vcpkg/installed/`, gitignored)
- Product / docs / build path changes may *reference* `lib/…`; they must not change submodule content
- If a task seems to require changing ImGui or vcpkg internals, **stop and ask** — pin updates are deliberate

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

## Timeline geometry ↔ minimap (required)

**Whenever timeline geometry or fetch units change**, update and verify the timeline minimap in the **same change set**. Do not land constant edits without a minimap pass.

| Constant | Role |
|----------|------|
| `ALPH_LOOKBACK_WINDOW_SECONDS` | G-segment length (**640s** = 10×64s) |
| `ALPH_SUBSEGMENT_SECONDS` / `ALPH_SUBSEGMENT_MS` / `SegmentDiskCache::kChunkMs` | Subsegment / disk / HTTP unit (**64s**) |
| `kTimelineChunkMs` | Network interval span (must match disk grid) |
| Genesis + `meters_per_second` + `timeline_origin_ms` | G_seg numbering and Z map |

Checklist: `draw_timeline_minimap_` uses no hard-coded 600/60/10min (sub-ticks = `ALPH_SUBSEGMENT_SECONDS`); presenter planes/`cam_k`/camera step match segment length; docs match (`docs/layers/app.md`, `network.md`, `segment-disk-cache.md`). When **user-facing** timeline/fill text changes, also update HTML under `docs/*.html` (hub: `docs/index.html`). Entry: `BlockflowOverlay::draw_timeline_minimap_`.

## Related gates

- **VnV (verification & validation):** see `vnv/TESTING.md` (taxonomy) and `vnv/README.md` (run/sync).
  - **Unit** (one `src/` area): `vnv/mod/tests/<area>/` — `.\scripts\run_vnv.ps1` default. **mod ≠ modularization.**
  - **Integration** (multi-module): `vnv/integration/` (scaffold).
  - **System · functional** (multi-system): `vnv/int/` — `.\scripts\run_vnv.ps1 -Int` / `-All`.
  - **System · bench** (perf): `vnv/bench/` — `.\scripts\run_vnv.ps1 -Bench` (opt-in).
  - After adding/removing a **shared library** or VnV project: edit `vnv/manifest/*.json`, then `.\scripts\sync_solutions.ps1`.
  - Drift check: `.\scripts\sync_solutions.ps1 -CheckOnly`.
- Vulkan validation: follow `.grok/skills/vulkan-validator/SKILL.md` before committing/pushing graphics changes.
- Build speed / PCH / IWYU / adding systems: see `docs/build-performance.md`. Use `scripts/bench_build.ps1` before claiming compile-time improvements.

## Build hygiene (PCH / includes)

- Product `.cpp` files start with `#include "<area>/pch.h"` (`graphics/`, `network/`, `engine/`, `app/`).
- **Platform TUs count as product TUs:** `src/*/platform/*_win32.cpp` must include that layer’s pch first (MSVC `PrecompiledHeader=Use`). See [docs/platform.md](docs/platform.md).
- Audit: `python scripts/check_pch.py` and `python scripts/check_include_boundary.py` before claiming Windows build green after adding TUs.
- Do not put frequently edited product headers into PCH files.
- Prefer forward declarations in headers; keep `gpu_pub_lib.h` Vulkan-free.
- **App never includes Vulkan:** do not `#include` `graphics/platform/gpu_platform.hpp` (or any header that pulls `vulkan/vulkan.h`) from `src/app/**`. The app vcxproj has no Vulkan include path. Forward-declare thin hooks or use `gpu_pub_lib.h` only.
- **Dual-track:** after changes under `src/*/platform/**`, CMake, or platform deps, MSVC product build (`sln/alephium_visualizer.sln` Debug|x64) is required before done — Linux CI alone is not enough.
- New static-lib systems: copy `sln/_template_system.vcxproj.example` and add a project-local PCH.
- OS / WSI / process / cache-root code goes in `platform/*_<os>.cpp`, not shared product TUs.
