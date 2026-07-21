# Platform source layout

OS- and WSI-specific code lives in **dedicated translation units** selected at build time. Shared product TUs must not include `windows.h` / X11 / GLFW headers.

## Layout

```text
src/app/platform/
  app_platform.hpp           # host window API
  app_platform_win32.cpp     # Win32 message loop, borderless fullscreen
  app_platform_linux.cpp     # GLFW host + F11/Esc

src/graphics/platform/
  gfx_platform.hpp           # surface, client size, ImGui platform, screenshot, log
  gfx_platform_win32.cpp
  gfx_platform_linux.cpp     # GLFW surface, ImGui_ImplGlfw, stderr log

src/network/platform/
  net_platform.hpp           # cache root dir, process private bytes
  net_platform_win32.cpp
  net_platform_linux.cpp     # XDG cache + /proc/self/status
```

## Build rules

| System | Selection |
|--------|-----------|
| MSVC (`sln/*.vcxproj`) | Compile only `*_win32.cpp` |
| CMake (`CMakeLists.txt`) | `if(WIN32)` → win32 else → linux |

**MSVC PCH:** each `*_win32.cpp` must `#include` the layer pch first (`app/pch.h`, `graphics/pch.h`, or `network/pch.h`) — same rule as other product TUs.

```text
# Audit (repo root) — fails if a PCH=Use .cpp misses the leading include
python3 scripts/check_pch.py
```

### Checklist: new platform `.cpp`

1. Name `src/<layer>/platform/<area>_platform_<os>.cpp`  
2. First line: `#include "<layer>/pch.h"` (MSVC projects)  
3. Add TU only to the correct OS list (vcxproj: win32 only; CMake: `if(WIN32)` branch)  
4. No product headers that change often in PCH  
5. Run `python3 scripts/check_pch.py`

Linux steps: [linux.md](linux.md).  
VnV mod on Linux: CMake targets `mod_domain` / `mod_network` + `scripts/run_vnv.sh`.

**Headless:** `EngineCreateInfo.headless` → `VK_EXT_headless_surface` (no OS window). Screenshots use GPU swapchain readback (`TRANSFER_SRC` + `gfx_platform_write_png_rgba`). Shared helpers in `gfx_platform_common.cpp`.

Never link both `*_win32` and `*_linux` into the same target. GLFW is linked on Linux/CMake only (not the MSVC product).

## Include isolation (app must stay Vulkan-free)

The **app** MSVC project does **not** add `$(VULKAN_SDK)\Include`. Including any header that pulls `<vulkan/vulkan.h>` fails with **C1083**.

| Consumer | May include |
|----------|-------------|
| **app** (product + harnesses) | `gpu_pub_lib.h`, `app/platform/app_platform.hpp`, engine public APIs |
| **app platform** `*_win32` / `*_linux` | OS headers + **forward decls** of thin graphics hooks — **not** `gfx_platform.hpp` today |
| **graphics** product TUs | `gfx_platform.hpp`, Vulkan, layer pch |
| **network** product TUs | `net_platform.hpp`, curl/cJSON, layer pch |

**Incident (2026-07):** `app_platform_*.cpp` included `graphics/platform/gfx_platform.hpp` (which includes Vulkan) → Windows app link/compile broke. Fix: forward-declare `gfx_platform_configure_headless(...)` in the app platform TU (linked via `graphics.lib`). Longer-term: split a Vulkan-free host-hooks header (see [ROADMAP](ROADMAP.md) P1).

## Dual-track smoke

Linux CMake/CI green does **not** prove Windows. After any change under `src/*/platform/**`, CMake, or vcpkg platform deps:

| Track | Gate |
|-------|------|
| **Windows (required for product)** | `msbuild sln\alephium_visualizer.sln /p:Configuration=Debug /p:Platform=x64` |
| **Linux** | CMake product + `scripts/run_vnv.sh` (or GitHub Actions) |
| **PCH hygiene** | `python scripts/check_pch.py` |

## Contracts

- **App** owns the event loop and fullscreen; graphics only observes client size via `gfx_platform_get_window_size`.
- **Graphics** public types stay Vulkan-free (`gpu_pub_lib.h`); native handles remain `void*` in `EngineCreateInfo`.
- **Network** uses `net_platform_cache_root()` + `std::filesystem` for paths (no hard-coded separators).

See also: [linux.md](linux.md), [ROADMAP.md](ROADMAP.md).
