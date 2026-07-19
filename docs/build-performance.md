# Build performance (MSVC / sln)

**Status: active.** Layer map: [docs/layers/README.md](layers/README.md). Graphics rebuild notes also linked from [layers/graphics.md](layers/graphics.md).

## Goals

- Faster **cold** Debug|x64 rebuilds (target ~2× vs pre-PCH baseline).
- Faster **incremental** rebuilds as systems grow.
- Shared flags so new systems inherit `/MP` + PCH patterns.

## Sample numbers (this machine, Debug|x64)

| Label | clean_rebuild | incr `graphics_system` | incr adapter | incr `main` |
|-------|---------------|------------------------|--------------|-------------|
| after PCH + `/MP` (shaders always rebuild) | ~13.9 s | ~4.6 s | ~2.8 s | ~3.3 s |
| + incremental shaders | ~13.0 s | ~3.2 s | ~2.2 s | ~1.9 s |

Pre-change full solution rebuilds in this repo commonly landed in the **~25–30 s** range (with full shader wipe each graphics build). After PCH + `/MP` + shader stamp, clean rebuild is roughly **~2×** faster; incremental `main.cpp` is ~**1.5–2×** faster than the PCH-only intermediate.

## Benchmark

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bench_build.ps1 -Label "my_label"
```

Results append to `build/bench/build_times.csv` (clean rebuild + touch three hot files).

## Shared props

`sln/Directory.Build.props` (auto-imported for projects under `sln/`):

- `MultiProcessorCompilation` (`/MP`)
- C++17, conformance mode
- `RepoRoot` / default OutDir-IntDir helpers

Project-specific include paths and PCH file names stay in each `.vcxproj`.

## Precompiled headers

| Project | Create TU | Header | Notes |
|---------|-----------|--------|--------|
| graphics | `src/graphics/pch.cpp` | `graphics/pch.h` | STL + glm + Vulkan |
| network | `src/network/pch.cpp` | `network/pch.h` | STL + curl + cJSON |
| blockviz_engine | `src/engine/pch.cpp` | `engine/pch.h` | STL + glm |
| alephium_visualizer | `src/app/pch.cpp` | `app/pch.h` | STL + windows + glm |

**Rules**

- First line of product `.cpp`: `#include "<project>/pch.h"` (qualified so projects do not pick the wrong `pch.h`).
- PCH = **third-party + STL only** — not product headers that change often.
- `.c` files and ImGui third-party TUs: `PrecompiledHeader=NotUsing`.

## IWYU (include what you use)

Policy:

1. Headers: forward-declare types used only as pointers/refs; include only what the interface needs.
2. Sources: `pch.h` → matching `.hpp` → only what the `.cpp` uses.
3. Public `gpu_pub_lib.h` stays **Vulkan-free**.
4. Prefer not including `graphics_system.hpp` outside graphics (factory returns `IGraphicsSystem*`).

Audit helper:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/include_audit.ps1
```

## PIMPL / system boundaries

- Prefer **public system headers without Vulkan/curl/imgui**.
- `GraphicsSystem` concrete type lives for the graphics lib only; host uses `create_graphics_system()` / `IGraphicsSystem`.
- Future systems: `src/<name>/` + `sln/<name>.vcxproj` from `sln/_template_system.vcxproj.example`, own `pch.h` / `pch.cpp`.

## Adding a new system lib

1. Copy `sln/_template_system.vcxproj.example` → `sln/mysystem.vcxproj`.
2. Create `src/mysystem/pch.h`, `pch.cpp`, sources.
3. Register in `vnv/manifest/libraries.json` (id, path, guid, depends_on, `in_product_sln` / `in_vnv_sln`).
4. Run `.\scripts\sync_solutions.ps1` (updates `alephium_visualizer.sln` + `blockviz_vnv.sln`).
5. Add `ProjectReference` from the app and/or engine; link in `AdditionalDependencies` as needed.
6. `.\scripts\run_vnv.ps1 -All` then run bench once to confirm no large compile regression.

See `vnv/README.md` for the full VnV layout (mod / int / bench).

## Related

- Vulkan validation gate: `.grok/skills/vulkan-validator/SKILL.md`
- Main version tags: `AGENTS.md`
