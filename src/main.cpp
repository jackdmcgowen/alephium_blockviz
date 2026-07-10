#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include "commands.h"
#include "config.h"
#include "app/app_identity.hpp"
#include "app/blockflow_overlay.hpp"
#include "app/camera_state.hpp"
#include "domain/block_scene.hpp"
#include "engine/blockviz_engine_api.hpp"
#include "adapters/alephium/network_poller.hpp"
#include "alph_block.hpp"
#include "graphics/gpu_pub_lib.h"
#include <windows.h>

// Window defaults (also defined in vulkan_engine.hpp for legacy; keep host self-contained)
#ifndef WDW_WIDTH
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024
#endif

// Set by NetworkPoller on the network thread; UI must not call commands.c.
extern "C" CURL* curl;
const char* baseUrl;

static volatile bool keepRunning = true;
static BlockScene scene;
static CameraState camera;
static IBlockvizEngine* engine = nullptr;
static BlockflowOverlay* overlay = nullptr;

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

    case WM_DESTROY:
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

    curl_global_init(CURL_GLOBAL_DEFAULT);

    engine = create_blockviz_engine();
    overlay = new BlockflowOverlay(camera, *engine);

    // Host owns application identity; engine fills its own product identity.
    EngineCreateInfo create_info{};
    create_info.platform_instance = hInstance;
    create_info.window = hwnd;
    create_info.application = app_identity::make();
    engine->initialize(create_info);

    engine->set_scene(&scene);
    engine->set_camera(&camera);
    engine->set_ui_overlay(overlay);
    engine->start();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    ConfigArray config_array = load_configs("config.json");
    if (config_array.count == 0 || !config_array.configs[0].url)
    {
        printf("Failed to load config\n");
        engine->stop();
        destroy_blockviz_engine(engine);
        delete overlay;
        curl_global_cleanup();
        return -1;
    }

    printf("Using config url: %s\n", config_array.configs[0].url);

    NetworkPoller poller(scene, *engine);
    NetworkPoller::Config net_cfg;
    net_cfg.base_url = config_array.configs[0].url;
    net_cfg.lookback_ms = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    net_cfg.poll_interval_ms = static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;
    poller.start(net_cfg);

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
            keepRunning = false;

        Sleep(10);
    }

    poller.stop();
    engine->stop();
    destroy_blockviz_engine(engine);
    engine = nullptr;
    delete overlay;
    overlay = nullptr;
    free_configs(&config_array);
    curl_global_cleanup();
    return 0;
}
