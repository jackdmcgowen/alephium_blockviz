// Graphics visual regression harness.
// Fixed-size window + Debug FakeChain + fixed camera pose + screenshot.
// Working directory must be the repo root.
//
// Usage:
//   int_visual --list
//   int_visual --case fake_overview --out vnv/int/tests/visual/out/fake_overview/actual.png
//   int_visual --case fake_side_cam ...
//   int_visual --case fake_selection_sobel ...
//   int_visual --headless --case fake_overview ...

#include "app/pch.h"

#include "app/app_identity.hpp"
#include "app/camera_controller.hpp"
#include "app/platform/app_platform.hpp"
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
#include <vector>

#ifndef VIS_WDW_W
#define VIS_WDW_W 1280
#endif
#ifndef VIS_WDW_H
#define VIS_WDW_H 720
#endif

namespace fs = std::filesystem;

static IEngine* g_engine = nullptr;

static const char* kCases[] = {
    "fake_overview",
    "fake_side_cam",
    "fake_selection_sobel",
};
static constexpr int kCaseCount = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));

static bool is_known_case(const std::string& id)
{
    for (int i = 0; i < kCaseCount; ++i)
        if (id == kCases[i])
            return true;
    return false;
}

static void on_resize(void* /*user*/)
{
    if (g_engine)
        g_engine->on_resize();
}

static void pump_for_ms(int ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (app_platform_is_running() && std::chrono::steady_clock::now() < end)
    {
        app_platform_poll_events();
        app_platform_sleep_ms(10);
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

static void apply_end_camera_(CameraController& camera)
{
    camera.set_view_preset(CameraController::ViewPreset::End);
    // Settle blend + scroll
    for (int i = 0; i < 45; ++i)
        camera.tick(1.f / 30.f);
    camera.set_scroll_z(-48.f);
    for (int i = 0; i < 15; ++i)
        camera.tick(1.f / 30.f);
}

static void apply_side_camera_(CameraController& camera)
{
    camera.set_view_preset(CameraController::ViewPreset::Side);
    for (int i = 0; i < 45; ++i)
        camera.tick(1.f / 30.f);
    camera.set_scroll_z(-48.f);
    for (int i = 0; i < 15; ++i)
        camera.tick(1.f / 30.f);
}

// Prefer confirmed tip hash; fall back to any graph node.
static std::string pick_selection_hash_(BlockScene& scene)
{
    for (uint32_t lane = 0; lane < static_cast<uint32_t>(BlockScene::kLaneCount); ++lane)
    {
        if (!scene.frontier_valid(lane))
            continue;
        const NodeId tip = scene.confirmed_tip_hash(lane);
        if (!tip.empty())
            return tip;
    }
    const std::vector<NodeId> tips = scene.tip_ids();
    if (!tips.empty() && !tips.front().empty())
        return tips.front();
    const auto nodes = scene.nodes_snapshot_unsorted();
    for (const auto& n : nodes)
    {
        if (!n.id.empty())
            return n.id;
    }
    return {};
}

static void print_usage()
{
    std::printf(
        "graphics_visual_tests — capture deterministic FakeChain frame\n"
        "  --case <id>     Case id (default: fake_overview)\n"
        "  --out <path>    Output PNG path (default: vnv/int/tests/visual/out/<case>/actual.png)\n"
        "  --warmup-ms N   Override warmup milliseconds\n"
        "  --width N --height N\n"
        "  --headless      VK_EXT_headless_surface (no DISPLAY/window)\n"
        "  --list\n"
        "Cases:\n");
    for (int i = 0; i < kCaseCount; ++i)
        std::printf("  %s\n", kCases[i]);
}

int main(int argc, char** argv)
{
    std::string case_id = "fake_overview";
    std::string out_path;
    int warmup_ms = 4500;
    int width = VIS_WDW_W;
    int height = VIS_WDW_H;
    bool list_only = false;
    bool headless = false;

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
        else if (std::strcmp(argv[i], "--headless") == 0)
            headless = true;
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
        for (int i = 0; i < kCaseCount; ++i)
            std::printf("%s\n", kCases[i]);
        return 0;
    }

    if (out_path.empty())
        out_path = "vnv/int/tests/visual/out/" + case_id + "/actual.png";

    if (!is_known_case(case_id))
    {
        std::printf("[vis] unsupported case '%s'\n", case_id.c_str());
        print_usage();
        return 2;
    }

    {
        fs::path p(out_path);
        if (p.has_parent_path())
            fs::create_directories(p.parent_path());
        std::error_code ec;
        fs::remove(p, ec);
    }

    EngineCreateInfo create_info{};
    create_info.application = app_identity::make();
    create_info.enable_validation = false;
    create_info.headless = headless;
    create_info.width = static_cast<uint32_t>(width);
    create_info.height = static_cast<uint32_t>(height);
    if (!app_platform_create_window(&create_info, "Blockviz Visual Tests",
                                    static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height)))
    {
        std::printf("[vis] create window failed\n");
        return 1;
    }
    create_info.width = static_cast<uint32_t>(width);
    create_info.height = static_cast<uint32_t>(height);

    AppPlatformCallbacks cb{};
    cb.on_resize = &on_resize;
    app_platform_set_callbacks(cb);

    BlockScene scene;
    CameraController camera;
    camera.set_aspect(static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1));
    // Default End pose (matches historical fake_overview).
    camera.set_scroll_z(-48.f);
    camera.add_look_delta(0.f, 180.f);
    for (int i = 0; i < 30; ++i)
        camera.tick(1.f / 30.f);

    IEngine* engine = create_engine();
    g_engine = engine;

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
    engine->set_ui_overlay(nullptr);

    engine->start();

    app_platform_show_window(create_info.window);
    app_platform_raise_window(create_info.window);

    std::printf("[vis] case=%s warmup_ms=%d out=%s\n", case_id.c_str(), warmup_ms, out_path.c_str());
    pump_for_ms(warmup_ms);

    if (case_id == "fake_side_cam")
        apply_side_camera_(camera);
    else
        apply_end_camera_(camera);

    if (case_id == "fake_selection_sobel")
    {
        const std::string hash = pick_selection_hash_(scene);
        if (hash.empty())
        {
            std::printf("[vis] FAIL: no block hash for selection (scene empty?)\n");
            engine->stop();
            engine->free_systems();
            destroy_engine(engine);
            g_engine = nullptr;
            delete presenter;
            app_platform_destroy_window(create_info.window);
            return 1;
        }
        std::printf("[vis] selection hash=%.16s…\n", hash.c_str());
        engine->set_selection(hash);
        // Allow publish + outline path a few frames.
        pump_for_ms(800);
        for (int i = 0; i < 10; ++i)
            camera.tick(1.f / 30.f);
    }

    pump_for_ms(500);

    engine->request_screenshot(out_path.c_str());
    const bool got = wait_for_file(out_path, 8000);

    engine->stop();
    engine->free_systems();
    destroy_engine(engine);
    g_engine = nullptr;
    delete presenter;

    app_platform_destroy_window(create_info.window);

    if (!got)
    {
        std::printf("[vis] screenshot not written: %s\n", out_path.c_str());
        return 1;
    }
    std::printf("[vis] wrote %s\n", out_path.c_str());
    return 0;
}
