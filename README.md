# alephium_blockviz

Cross-platform **Vulkan + ImGui** visualizer for **Alephium BlockFlow** — sixteen shards polled from official (or configured) nodes, plus offline **FakeChain** (Debug). Switch **mainnet** / **testnet** / **Debug** in the Network panel or via `user_prefs.json` + `config.json`.

| | |
|--|--|
| **Host** | Alephium BlockFlow **1.2.0** (`src/app/app_identity.hpp`) |
| **Engine** | BlockvizEngine **1.2.0** (`src/engine/engine_identity.hpp`) |
| **Windows** | `sln/alephium_visualizer.sln` (MSVC) |
| **Linux** | CMake + GLFW — [docs/linux.md](docs/linux.md) |
| **VnV** | `vnv/` · [TESTING.md](vnv/TESTING.md) · `sln/blockviz_vnv.sln` · `.\scripts\run_vnv.ps1` / `./scripts/run_vnv.sh` |
| **CI** | [`.github/workflows/linux-ci.yml`](.github/workflows/linux-ci.yml) (Ubuntu: product build + mod VnV) |

Release tags on `main`: `app-vMAJOR.MINOR.PATCH` + `engine-v…` — see [AGENTS.md](AGENTS.md).

## Platform support

| Platform | Host | Build | GUI product | Headless | Mod VnV | Int visual | CI |
|----------|------|-------|-------------|----------|---------|------------|-----|
| **Windows x64** | Win32 | MSVC `sln/` | Supported | Supported | `run_vnv.ps1` | Supported | Local dual-track smoke |
| **Linux x64** | GLFW | CMake + Ninja | Supported | Supported (`VK_EXT_headless_surface`) | `run_vnv.sh` | Supported (DISPLAY or headless goldens) | GitHub Actions (mod) |
| **macOS** | — | — | **Not supported** | — | — | — | Unscheduled ([ROADMAP](docs/ROADMAP.md) P2) |

**Dual-track rule:** Linux CI green does **not** prove Windows MSVC. After changes under `src/*/platform/**`, CMake, or vcpkg platform deps, smoke **both** tracks ([docs/platform.md](docs/platform.md)).

## Features (short)

- Network panel: domain switch, loading/cache HUD, feed
- 3D BlockFlow cubes + tip/selection **Sobel** outlines (app-fed colors)
- Selection + dependency BFS fan, camera End / Side / Live (keys **1** / **2** / **3**)
- Timeline minimap (segment jump)
- **F3** frame profiler HUD · **F12** GPU screenshot (readback while swapchain image is still acquired) · **F11** fullscreen

## Architecture

Layered libraries — living goals and plans:

| Layer | Doc |
|-------|-----|
| Map | [docs/layers/README.md](docs/layers/README.md) |
| App | [docs/layers/app.md](docs/layers/app.md) |
| Engine | [docs/layers/engine.md](docs/layers/engine.md) |
| Graphics | [docs/layers/graphics.md](docs/layers/graphics.md) |
| Network | [docs/layers/network.md](docs/layers/network.md) |
| Platform | [docs/platform.md](docs/platform.md) · [docs/linux.md](docs/linux.md) |

**Roadmap / priorities:** [docs/ROADMAP.md](docs/ROADMAP.md).

Historical design archives (not live backlogs): [modularization](docs/graphics-modularization-design.md), [confirmed tips](docs/blockflow-confirmed-tips-design.md). Build speed: [docs/build-performance.md](docs/build-performance.md).

## Build & run

### Windows (MSVC)

1. Install [Vulkan SDK](https://vulkan.lunarg.com/) and MSVC (VS 2022+).
2. `git submodule update --init --recursive` (Dear ImGui + vcpkg).
3. From the repo root:

```bat
install_deps.bat
```

4. Open `sln/alephium_visualizer.sln`, build **Debug|x64** or **Release|x64**.
5. Run with **cwd = repo root** so `config.json`, `user_prefs.json`, and `resource/` resolve.

### Linux (CMake)

See **[docs/linux.md](docs/linux.md)** (packages or vcpkg, Ninja, run from repo root):

```bash
./install_deps.sh   # optional vcpkg path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build build
./build/bin/alephium_visualizer
```

Prefer **Network panel → Debug** (FakeChain) for offline demos without a live node.

### Config

```json
[
  {"url": "https://node.mainnet.alephium.org"},
  {"url": "https://node.testnet.alephium.org"}
]
```

Boot domain prefers `user_prefs.json` (`mainnet` / `testnet` / `debug`); otherwise the first config URL (substring match).

## Screenshots

FakeChain (Debug) product UI — Network + Block rails, tip cubes, timeline minimap.

![](docs/images/screenshot_1.png)

![](docs/images/screenshot_2.png)

![](docs/images/screenshot_3.png)
