#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include "alph_block.hpp"
#include "app/app_identity.hpp"
#include "app/blockflow_overlay.hpp"
#include "app/camera_controller.hpp"
#include "app/scene_presenter.hpp"
#include "config.h"
#include "domain/block_scene.hpp"
#include "engine/blockviz_engine_api.hpp"
#include "graphics/gpu_pub_lib.h"

#include <windows.h>

// Host window defaults (no graphics/network backends in this TU).
#ifndef WDW_WIDTH
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024
#endif

static volatile bool keepRunning = true;
static BlockScene scene;
static CameraController camera;
static IBlockvizEngine* engine = nullptr;
static BlockflowOverlay* overlay = nullptr;
static ScenePresenter* scene_presenter = nullptr;

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

    // Product engine owns GraphicsSystem + NetworkSystem.
    engine = create_blockviz_engine();
    overlay = new BlockflowOverlay(camera, *engine);
    scene_presenter = new ScenePresenter(scene);

    EngineCreateInfo create_info{};
    create_info.platform_instance = hInstance;
    create_info.window = hwnd;
    create_info.application = app_identity::make();
    engine->initialize(create_info);

    engine->set_scene(&scene);
    engine->set_camera(&camera);
    engine->set_frame_source(scene_presenter);
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
        return -1;
    }

    printf("Using config url: %s\n", config_array.configs[0].url);

    NetworkSystemConfig net_cfg;
    net_cfg.base_url = config_array.configs[0].url;
    net_cfg.lookback_ms = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    net_cfg.poll_interval_ms = static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;
    blockviz_engine_start_network(engine, net_cfg);

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

    engine->stop();
    destroy_blockviz_engine(engine);
    engine = nullptr;
    delete scene_presenter;
    scene_presenter = nullptr;
    delete overlay;
    overlay = nullptr;
    free_configs(&config_array);
    return 0;
}
