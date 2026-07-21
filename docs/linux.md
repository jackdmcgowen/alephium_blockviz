# Linux build (CMake + GLFW)

Platform TUs: [platform.md](platform.md). Product still builds on Windows via `sln/`.

## Prerequisites

- C++17 compiler (GCC 11+ / Clang 14+)
- CMake ≥ 3.20, Ninja (recommended), Python 3
- Vulkan loader + ICD (Mesa or vendor driver)
- `glslc` (Vulkan SDK or `glslang` from vcpkg / distro `glslang-tools`)

### Distro packages (Ubuntu/Debian, needs sudo)

```bash
sudo apt-get install -y build-essential cmake ninja-build python3 \
  libvulkan-dev vulkan-tools glslang-tools libglfw3-dev \
  libcurl4-openssl-dev libcjson-dev libglm-dev zlib1g-dev
```

### vcpkg (no root; works in this repo)

```bash
git submodule update --init --recursive
./install_deps.sh
```

Manifest (`vcpkg.json`) pulls: curl, cjson, glm, zlib, glfw3, vulkan-headers, glslang.

## Configure & build

From **repo root**:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build
```

Without vcpkg (system packages only):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run

CWD must be the **repo root** (`config.json`, `resource/`):

```bash
./build/bin/alephium_visualizer
```

Tips:

- Prefer **FakeChain**: Network panel → Debug (offline cubes).
- Validation layers: install `vulkan-validationlayers`; Debug builds write `build/debug.log`.
- Segment disk cache: `~/.cache/AlephiumBlockViz/cache/<domain>/`.

## VnV (mod, CPU-only)

Same suites as Windows `.\scripts\run_vnv.ps1` (default mod):

```bash
./scripts/run_vnv.sh                 # build mod_domain + mod_network, run both
./scripts/run_vnv.sh --skip-build    # run existing binaries only
./scripts/run_vnv.sh --ctest         # optional ctest driver
```

Binaries: `build/bin/mod_domain`, `build/bin/mod_network`.

`int_visual` / `bench_*` are **not** on Linux yet (use Windows `run_vnv.ps1 -Int` / `-Bench`).

## Platform sources linked on Linux

| Target | TU |
|--------|-----|
| app | `src/app/platform/app_platform_linux.cpp` (GLFW) |
| graphics | `src/graphics/platform/gfx_platform_linux.cpp` |
| network | `src/network/platform/net_platform_linux.cpp` |

MSVC continues to compile only `*_win32.cpp`.
