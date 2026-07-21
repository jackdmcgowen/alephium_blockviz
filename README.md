# alephium_blockviz

Windows Vulkan + ImGui visualizer for **Alephium BlockFlow** — sixteen shards polled from official (or configured) nodes. Switch **mainnet** / **testnet** in the Network panel or via `config.json`.

| | |
|--|--|
| **Host** | Alephium BlockFlow **0.7.0** (`app_identity`) |
| **Engine** | BlockvizEngine **0.8.0** (`engine_identity`) |
| **Solution** | `sln/alephium_visualizer.sln` |
| **VnV** | `vnv/` · `sln/blockviz_vnv.sln` · `.\scripts\run_vnv.ps1` (mod/int) |

## Architecture

Layered libraries — living goals and plans:

| Layer | Doc |
|-------|-----|
| Map | [docs/layers/README.md](docs/layers/README.md) |
| App | [docs/layers/app.md](docs/layers/app.md) |
| Engine | [docs/layers/engine.md](docs/layers/engine.md) |
| Graphics | [docs/layers/graphics.md](docs/layers/graphics.md) |
| Network | [docs/layers/network.md](docs/layers/network.md) |

**Roadmap / priorities:** [docs/ROADMAP.md](docs/ROADMAP.md).

Historical design archives (not live backlogs): [modularization](docs/graphics-modularization-design.md), [confirmed tips](docs/blockflow-confirmed-tips-design.md). Build speed: [docs/build-performance.md](docs/build-performance.md). Version tags on `main`: [AGENTS.md](AGENTS.md).

## Build & run

**Windows:** MSVC / `sln/alephium_visualizer.sln`  
**Linux:** CMake + GLFW — [docs/linux.md](docs/linux.md) · platform TUs: [docs/platform.md](docs/platform.md)

1. Install [Vulkan SDK](https://vulkan.lunarg.com/) and MSVC (VS 2022+).
2. `git submodule update --init --recursive` (Dear ImGui + vcpkg).
3. From the repo root:

```bat
install_deps.bat
```

4. Open `sln/alephium_visualizer.sln`, build **Debug|x64** or **Release|x64**.
5. Run with cwd = repo root so `config.json` and `resource/` resolve.

```json
[
  {"url": "https://node.mainnet.alephium.org"},
  {"url": "https://node.testnet.alephium.org"}
]
```

First config entry chooses the boot domain (substring match on `mainnet` / `testnet`).

## Screenshots

![](docs/images/screenshot_1.png)
![](docs/images/screenshot_2.png)
![](docs/images/screenshot_3.png)
