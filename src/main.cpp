#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include "commands.h"
#include "config.h"
#include "vulkan_renderer.hpp"
#include <windows.h>

extern "C" CURL* curl;
const char * baseUrl;

static volatile bool keepRunning = true; // Global exit flag

static VulkanRenderer renderer;

static void format_output(cJSON* json)
{
    char* formatted = cJSON_Print(json);
    if (formatted)
    {
        printf("%s\n", formatted);
        free(formatted);
    }
}

void get_heights(int heights[4][4])
{
    for (int fromGroup = 0; fromGroup < 4; fromGroup++)
    {
        for (int toGroup = 0; toGroup < 4; toGroup++)
        {
            heights[fromGroup][toGroup] = 0;
            int h = get_height(fromGroup, toGroup);
            printf("Chain Height for shard [%d,%d]: %d\n", fromGroup, toGroup, h);
            heights[fromGroup][toGroup] = h;
        }
    }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SIZE:
        renderer.Resize();
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main()
{
    //int heights[4][4];
    int64_t lastPollTs;
    std::vector<cJSON*> blockQueue;
    int64_t lastAddTime = 0;

    // Window setup
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"Alephium_BlockFlow";

    // Load icon
    wc.hIcon = (HICON)LoadImage(NULL, L"alephium-logo-round.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);

    RegisterClass(&wc);

    HWND hwnd = CreateWindow
        (
        wc.lpszClassName,
        wc.lpszClassName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WDW_WIDTH, WDW_HEIGHT,
        NULL, NULL, hInstance, NULL
        );

    if (!hwnd)
    {
        printf("Failed to create window\n");
        return -1;
    }

    // Vulkan init
    renderer.Init(hInstance, hwnd);
    renderer.Start();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    

    ConfigArray config_array = load_configs("config.json");
    if (config_array.count == 0 || !config_array.configs[0].url)
    {
        printf("Failed to load config\n");
        return -1;
    }
    baseUrl = config_array.configs[0].url;

    curl = curl_easy_init();
    if (!curl)
    {
        printf("curl init failed\n");
        free_configs(&config_array);
        return -1;
    }

    lastPollTs = static_cast<int64_t>(time(NULL) - ALPH_TARGET_POLL_SECONDS) * 1000; // 8 seconds ago

    // Main loop with polling and rendering
    MSG msg = { 0 };
    while (keepRunning)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                keepRunning = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Check for Esc key
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            keepRunning = false;
        }

        int64_t now = (int64_t)time(NULL) * 1000;
        if (now - lastPollTs >= ( ALPH_TARGET_POLL_SECONDS * 1000 )) // 8 seconds
        {
            printf("\nPolling blockflow at %lld\n", now);
            ResponseData response = { NULL, 0, 0, 0 };
            cJSON* obj = get_blockflow_blocks_with_events(lastPollTs, now);

            if (obj)
            {
                GET_OBJECT_ITEM(obj, blocksAndEvents);
                if (blocksAndEvents && cJSON_IsArray(blocksAndEvents))
                {
                    int count = cJSON_GetArraySize(blocksAndEvents);
                    int totalBlocks = 0;

                    for (int i = 0; i < count; i++)
                    {
                        cJSON* shard = cJSON_GetArrayItem(blocksAndEvents, i);
                        if (shard && cJSON_IsArray(shard))
                        {
                            int blocksEventsCount = cJSON_GetArraySize(shard);
                            for (int j = 0; j < blocksEventsCount; ++j)
                            {
                                cJSON* iter = cJSON_GetArrayItem(shard, j);
                                GET_OBJECT_ITEM( iter, block );
                                if (block)
                                {
                                    renderer.Add_Block(block);
                                    totalBlocks++;
                                }

                            }
                        }
                    }
                    printf("Polled %d blocks\n", totalBlocks);
                }
                lastPollTs = now;
                cJSON_Delete(obj);
            }
        }

        Sleep(10); // Avoid tight loop

    }

    // Cleanup
    renderer.Stop();
    curl_easy_cleanup(curl);
    free_configs(&config_array);
    for (auto block : blockQueue)
    {
        cJSON_Delete(block);
    }
    return 0;

}   /* main() */
