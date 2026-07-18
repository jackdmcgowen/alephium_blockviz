#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include "domain/alph_block.hpp"
#include "app/app_identity.hpp"
#include "app/blockflow_overlay.hpp"
#include "app/camera_controller.hpp"
#include "app/scene_presenter.hpp"
#include "app/config.h"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"
#include "graphics/gpu_pub_lib.h"
#include "network/network_domain.hpp"

#include <string>
#include <vector>
#include <windows.h>

// Host window defaults (no graphics/network backends in this TU).
#ifndef WDW_WIDTH
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024
#endif

static volatile bool keepRunning = true;
static bool engine_stopped = false;
static BlockScene scene;
static CameraController camera;
static IEngine* engine = nullptr;
static BlockflowOverlay* overlay = nullptr;
static ScenePresenter* scene_presenter = nullptr;

static void stop_engine_once()
{
    if (engine_stopped || !engine)
        return;
    engine->stop();
    engine_stopped = true;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SIZE:
        if (engine)
            engine->on_resize();
        break;

    case WM_CLOSE:
        // Stop render/network while HWND + surface are still valid, then destroy.
        keepRunning = false;
        stop_engine_once();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        keepRunning = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main()
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Alephium BlockFlow";
    wc.hIcon = (HICON)LoadImage(NULL, L"resource\\Alephium-Logo-round.ico", IMAGE_ICON, 0, 0,
                                LR_LOADFROMFILE | LR_DEFAULTSIZE);

    RegisterClass(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        wc.lpszClassName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WDW_WIDTH, WDW_HEIGHT,
        NULL, NULL, hInstance, NULL);

    if (!hwnd)
    {
        printf("Failed to create window\n");
        return -1;
    }

    ConfigArray config_array = load_configs("config.json");
    if (config_array.count == 0 || !config_array.configs[0].url)
    {
        printf("Failed to load config\n");
        return -1;
    }

    std::vector<std::string> domain_urls;
    for (int i = 0; i < config_array.count; ++i)
    {
        if (config_array.configs[i].url)
            domain_urls.emplace_back(config_array.configs[i].url);
    }
    const NetworkDomain boot_domain =
        network_domain_from_url(config_array.configs[0].url);
    std::vector<const char*> url_ptrs;
    url_ptrs.reserve(domain_urls.size());
    for (const auto& u : domain_urls)
        url_ptrs.push_back(u.c_str());
    const std::string boot_url = network_domain_resolve_url(
        boot_domain, url_ptrs.empty() ? nullptr : url_ptrs.data(),
        static_cast<int>(url_ptrs.size()));
    printf("Using config url: %s (domain %s)\n", boot_url.c_str(),
           network_domain_label(boot_domain));

    // One init path: configure + register all systems, then init_systems / start once.
    engine = create_engine();

    EngineCreateInfo create_info{};
    create_info.platform_instance = hInstance;
    create_info.window = hwnd;
    create_info.application = app_identity::make();

    IGraphicsSystem* graphics = create_graphics_system();
    graphics->configure(create_info);
    engine->add_system(graphics);

    NetworkSystemConfig net_cfg;
    net_cfg.base_url = boot_url;
    net_cfg.domain = static_cast<int>(boot_domain);
    net_cfg.lookback_ms = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    net_cfg.poll_interval_ms = static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;

    INetworkSystem* network = create_network_system(scene, *engine);
    network->configure(net_cfg);
    engine->add_system(network);

    engine->init_systems();

    overlay = new BlockflowOverlay(camera, *engine);
    overlay->set_domain_urls(domain_urls);
    overlay->set_initial_domain(boot_domain);
    scene_presenter = new ScenePresenter(scene);
    engine->set_scene(&scene);
    engine->set_camera(&camera);
    engine->set_frame_source(scene_presenter);
    engine->set_ui_overlay(overlay);

    engine->start();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (keepRunning)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                keepRunning = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            keepRunning = false;
            stop_engine_once();
        }

        Sleep(10);
    }

    stop_engine_once();
    engine->free_systems();
    destroy_engine(engine);
    engine = nullptr;
    delete scene_presenter;
    scene_presenter = nullptr;
    delete overlay;
    overlay = nullptr;
    free_configs(&config_array);
    return 0;
}
