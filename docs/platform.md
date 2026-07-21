# Platform source layout

OS- and WSI-specific code lives in **dedicated translation units** selected at build time. Shared product TUs must not include `windows.h` / X11 / GLFW headers.

## Layout

```text
src/app/platform/
  app_platform.hpp           # host window API
  app_platform_win32.cpp     # Win32 message loop, borderless fullscreen
  app_platform_linux.cpp     # (planned) GLFW host

src/graphics/platform/
  gfx_platform.hpp           # surface, client size, ImGui platform, screenshot, log
  gfx_platform_win32.cpp
  gfx_platform_linux.cpp     # (planned)

src/network/platform/
  net_platform.hpp           # cache root dir, process private bytes
  net_platform_win32.cpp
  net_platform_linux.cpp     # (planned)
```

## Build rules

| System | Selection |
|--------|-----------|
| MSVC (`sln/*.vcxproj`) | Compile only `*_win32.cpp` |
| CMake (planned) | `if(WIN32)` → win32 else → linux |

Never link both `*_win32` and `*_linux` into the same target.

## Contracts

- **App** owns the event loop and fullscreen; graphics only observes client size via `gfx_platform_get_window_size`.
- **Graphics** public types stay Vulkan-free (`gpu_pub_lib.h`); native handles remain `void*` in `EngineCreateInfo`.
- **Network** uses `net_platform_cache_root()` + `std::filesystem` for paths (no hard-coded separators).

See also: Linux expansion plan / roadmap (desktop port).
