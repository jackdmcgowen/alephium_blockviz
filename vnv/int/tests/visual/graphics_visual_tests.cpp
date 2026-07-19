// Graphics visual regression harness (V1).
// Fixed-size window + Debug FakeChain + fixed camera + BitBlt screenshot.
// Working directory must be the repo root.
//
// Usage:
//   int_visual.exe --case fake_overview --out vnv/int/tests/visual/out/fake_overview/actual.png
//   int_visual.exe --list

#include "app/pch.h"

#include "app/app_identity.hpp"
#include "app/camera_controller.hpp"
#include "app/scene_presenter.hpp"
#include "domain/alph_block.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"
#include "graphics/gpu_pub_lib.h"
#include "network/network_domain.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef VIS_WDW_W
#define VIS_WDW_W 1280
#endif
#ifndef VIS_WDW_H
#define VIS_WDW_H 720
#endif

namespace fs = std::filesystem;

static volatile bool g_running = true;
static IEngine* g_engine = nullptr;

static LRESULT CALLBACK VisWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_engine)
            g_engine->on_resize();
        return 0;
    case WM_CLOSE:
        g_running = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void pump_for_ms(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    MSG msg{};
    while (g_running && std::chrono::steady_clock::now() < end)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static bool wait_for_file(const fs::path& path, int timeout_ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < end)
    {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::file_size(path, ec) > 64 && !ec)
            return true;
        pump_for_ms(50);
    }
    std::error_code ec;
    return fs::exists(path, ec) && fs::file_size(path, ec) > 64 && !ec;
}

static void print_usage()
{
    std::printf(
        "graphics_visual_tests — capture deterministic FakeChain frame\n"
        "  --case <id>     Case id (default: fake_overview)\n"
        "  --out <path>    Output PNG path (default: vnv/int/tests/visual/out/<case>/actual.png)\n"
        "  --warmup-ms N   Override warmup milliseconds\n"
        "  --width N --height N\n"
        "  --list\n");
}

int main(int argc, char** argv)
{
    std::string case_id = "fake_overview";
    std::string out_path;
    int warmup_ms = 4500;
    int width = VIS_WDW_W;
    int height = VIS_WDW_H;
    bool list_only = false;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            case_id = argv[++i];
        else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out_path = argv[++i];
        else if (std::strcmp(argv[i], "--warmup-ms") == 0 && i + 1 < argc)
            warmup_ms = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--list") == 0)
            list_only = true;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            print_usage();
            return 0;
        }
        else
        {
            std::printf("Unknown arg: %s\n", argv[i]);
            print_usage();
            return 2;
        }
    }

    if (list_only)
    {
        std::printf("fake_overview\n");
        return 0;
    }

    if (out_path.empty())
        out_path = "vnv/int/tests/visual/out/" + case_id + "/actual.png";

    if (case_id != "fake_overview")
    {
        std::printf("[vis] unsupported case '%s' (V1: fake_overview only)\n", case_id.c_str());
        return 2;
    }

    // Ensure output directory exists.
    {
        fs::path p(out_path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());
        std::error_code ec;
        fs::remove(p, ec);
    }

    // Prefer fixed client size; DPI may still affect BitBlt — document in README.
    SetProcessDPIAware();

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = VisWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"BlockvizVisualTests";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    RECT wr{ 0, 0, width, height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, L"Blockviz Visual Tests",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd)
    {
        std::printf("[vis] CreateWindow failed\n");
        return 1;
    }

    BlockScene scene;
    CameraController camera;
    camera.set_aspect(static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1));
    // Detach timeline and fix a stable overview pose.
    camera.set_scroll_z(-48.f);
    camera.add_look_delta(0.f, 180.f); // slight pitch up from default
    // Snap targets: tick a few large dt steps offline (render thread also ticks).
    for (int i = 0; i < 30; ++i)
        camera.tick(1.f / 30.f);

    IEngine* engine = create_engine();
    g_engine = engine;

    EngineCreateInfo create_info{};
    create_info.platform_instance = hInstance;
    create_info.window = hwnd;
    create_info.width = static_cast<uint32_t>(width);
    create_info.height = static_cast<uint32_t>(height);
    // Visual goldens: prefer no validation noise (still Debug build OK).
    create_info.enable_validation = false;
    create_info.application = app_identity::make();

    IGraphicsSystem* graphics = create_graphics_system();
    graphics->configure(create_info);
    engine->add_system(graphics);

    NetworkSystemConfig net_cfg;
    net_cfg.base_url = "debug://fake-chain";
    net_cfg.domain = static_cast<int>(NetworkDomain::Debug);
    net_cfg.lookback_ms = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    net_cfg.poll_interval_ms = static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;

    INetworkSystem* network = create_network_system(scene, *engine);
    network->configure(net_cfg);
    engine->add_system(network);

    engine->init_systems();

    ScenePresenter* presenter = new ScenePresenter(scene);
    engine->set_scene(&scene);
    engine->set_camera(&camera);
    engine->set_frame_source(presenter);
    engine->set_ui_overlay(nullptr); // scene-only capture intent (host still inits ImGui)

    engine->start();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    // Keep on top briefly so BitBlt is not occluded during capture.
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);

    std::printf("[vis] case=%s warmup_ms=%d out=%s\n", case_id.c_str(), warmup_ms, out_path.c_str());
    pump_for_ms(warmup_ms);

    // Re-assert fixed camera after network/timeline publishes.
    camera.set_scroll_z(-48.f);
    for (int i = 0; i < 10; ++i)
        camera.tick(1.f / 30.f);
    pump_for_ms(500);

    engine->request_screenshot(out_path.c_str());
    const bool got = wait_for_file(out_path, 8000);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    engine->stop();
    engine->free_systems();
    destroy_engine(engine);
    g_engine = nullptr;
    delete presenter;

    DestroyWindow(hwnd);

    if (!got)
    {
        std::printf("[vis] screenshot not written: %s\n", out_path.c_str());
        return 1;
    }
    std::printf("[vis] wrote %s\n", out_path.c_str());
    return 0;
}
