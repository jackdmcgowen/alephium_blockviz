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

## VnV

Same suites as Windows `.\scripts\run_vnv.ps1` where ported:

```bash
./scripts/run_vnv.sh                 # mod (default)
./scripts/run_vnv.sh --skip-build
./scripts/run_vnv.sh --ctest
./scripts/run_vnv.sh --int           # needs DISPLAY + GPU; golden compare
./scripts/run_vnv.sh --bench         # opt-in perf; needs DISPLAY + GPU
./scripts/run_vnv.sh --all           # mod + int
```

| Binary | Role |
|--------|------|
| `build/bin/mod_domain` | Domain / detail store |
| `build/bin/mod_network` | HttpIoPool |
| `build/bin/int_visual` | FakeChain PNG capture |
| `build/bin/bench_frame_profiler` | Frame timing JSON |

Image compare (Linux): `vnv/int/tests/visual/compare_images.py` (needs Pillow).

**CI:** `.github/workflows/linux-ci.yml` — build, **mod**, **headless int hard gate** (lavapipe + `goldens/linux_headless/`).

**Pre-merge smoke:**

```bash
./scripts/smoke_dual_track.sh           # PCH + mod + headless int
./scripts/smoke_dual_track.sh --skip-build
```

### Headless (no DISPLAY)

Uses **`VK_EXT_headless_surface`** + GPU swapchain readback for PNGs:

```bash
./build/bin/int_visual --headless --case fake_overview \
  --out vnv/int/tests/visual/out/fake_overview/actual.png
./scripts/run_vnv.sh --int --headless
# compares against vnv/int/tests/visual/goldens/linux_headless/fake_overview.png
```

Requires a Vulkan ICD (Mesa lavapipe is fine). Golden policy: [vnv/int/tests/visual/goldens/README.md](../vnv/int/tests/visual/goldens/README.md).

### GPU drivers (local)

- Mesa: `vulkan-tools` → `vulkaninfo` should list a device (RADV/ANV/lavapipe)
- NVIDIA: proprietary driver + Vulkan ICD
- Validation: `vulkan-validationlayers` for Debug logs in `build/debug.log`

## Platform sources linked on Linux

| Target | TU |
|--------|-----|
| app | `src/app/platform/app_platform_linux.cpp` (GLFW) |
| graphics | `src/graphics/platform/gfx_platform_linux.cpp` |
| network | `src/network/platform/net_platform_linux.cpp` |

MSVC continues to compile only `*_win32.cpp`.
