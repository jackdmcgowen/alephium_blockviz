// VnV bench harness — FakeChain steady frame + FrameTimingSnapshot medians.
// Working directory must be the repo root.
//
// Usage:
//   bench_frame_profiler --case fake_steady_frame --out vnv/bench/tests/out/fake_steady_frame/actual.json
//   bench_frame_profiler --list

#include "app/pch.h"

#include "app/app_identity.hpp"
#include "app/camera_controller.hpp"
#include "app/platform/app_platform.hpp"
#include "app/scene_presenter.hpp"
#include "domain/alph_block.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"
#include "graphics/gpu_pub_lib.h"
#include "network/fake/fake_chain_simulator.hpp"
#include "network/network_domain.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef BENCH_WDW_W
#define BENCH_WDW_W 1280
#endif
#ifndef BENCH_WDW_H
#define BENCH_WDW_H 720
#endif

namespace fs = std::filesystem;

static IEngine* g_engine = nullptr;

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
        app_platform_sleep_ms(5);
    }
}

static float percentile(std::vector<float> v, float p)
{
    if (v.empty())
        return 0.f;
    std::sort(v.begin(), v.end());
    const float idx = p * static_cast<float>(v.size() - 1);
    const size_t i0 = static_cast<size_t>(idx);
    const size_t i1 = (std::min)(i0 + 1, v.size() - 1);
    const float t = idx - static_cast<float>(i0);
    return v[i0] * (1.f - t) + v[i1] * t;
}

static void print_usage()
{
    std::printf(
        "graphics_bench_tests — sample FrameTimingSnapshot under FakeChain\n"
        "  --case <id>       Case id (default: fake_steady_frame)\n"
        "  --out <path>      Output JSON (default: vnv/bench/tests/out/<case>/actual.json)\n"
        "  --warmup-ms N     Warmup milliseconds (default 2000)\n"
        "  --samples N       Frames to sample after warmup (default 120)\n"
        "  --sample-ms N     Wall time between snapshot polls (default 16)\n"
        "  --width N --height N\n"
        "  --headless        VK_EXT_headless_surface (no DISPLAY/window)\n"
        "  --list\n"
        "\n"
        "Cases:\n"
        "  fake_steady_frame          fixed camera, default FakeChain density\n"
        "  fake_stress_instances      looser soft budgets (same scene)\n"
        "  fake_overdraw_end_z        dense bootstrap, End look down +Z\n"
        "  fake_overdraw_end_z_move   dense + scroll Z during sample\n"
        "  fake_bfs_end_z             dense + selection tip (dep walk / Sobel)\n");
}

static bool is_known_case(const std::string& id)
{
    return id == "fake_steady_frame" || id == "fake_stress_instances" ||
           id == "fake_overdraw_end_z" || id == "fake_overdraw_end_z_move" ||
           id == "fake_bfs_end_z";
}

static bool is_dense_case(const std::string& id)
{
    return id == "fake_overdraw_end_z" || id == "fake_overdraw_end_z_move" ||
           id == "fake_bfs_end_z";
}

// End preset: look along +Z into polar ring (timeline depth overdraw).
static void apply_end_camera_down_z(CameraController& camera, float scroll_z)
{
    camera.set_view_preset(CameraController::ViewPreset::End);
    for (int i = 0; i < 45; ++i)
        camera.tick(1.f / 30.f);
    camera.set_scroll_z(scroll_z);
    for (int i = 0; i < 20; ++i)
        camera.tick(1.f / 30.f);
}

static std::string pick_selection_hash(BlockScene& scene)
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

static void wait_min_blocks(BlockScene& scene, int min_blocks, int timeout_ms)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (app_platform_is_running() && std::chrono::steady_clock::now() < end)
    {
        if (scene.total_blocks() >= min_blocks)
            return;
        app_platform_poll_events();
        app_platform_sleep_ms(20);
    }
}

int main(int argc, char** argv)
{
    std::string case_id = "fake_steady_frame";
    std::string out_path;
    int warmup_ms = 2000;
    int samples = 120;
    int sample_ms = 16;
    int width = BENCH_WDW_W;
    int height = BENCH_WDW_H;
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
        else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
            samples = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--sample-ms") == 0 && i + 1 < argc)
            sample_ms = std::atoi(argv[++i]);
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
        std::printf("fake_steady_frame\n");
        std::printf("fake_stress_instances\n");
        std::printf("fake_overdraw_end_z\n");
        std::printf("fake_overdraw_end_z_move\n");
        std::printf("fake_bfs_end_z\n");
        return 0;
    }

    if (out_path.empty())
        out_path = "vnv/bench/tests/out/" + case_id + "/actual.json";

    if (!is_known_case(case_id))
    {
        std::printf("[bench] unsupported case '%s'\n", case_id.c_str());
        return 2;
    }
    const bool dense = is_dense_case(case_id);
    const bool moving = (case_id == "fake_overdraw_end_z_move");
    const bool bfs = (case_id == "fake_bfs_end_z");
    // Only raise defaults when caller left stock sample count (120).
    if ((case_id == "fake_stress_instances" || dense) && samples == 120)
        samples = 160;
    if (dense && warmup_ms == 2000)
        warmup_ms = 3500;
    if (samples < 8)
        samples = 8;

    constexpr int kDenseBootstrapHeights = 48;
    if (dense)
        FakeChainSimulator::set_bootstrap_heights_override(kDenseBootstrapHeights);
    else
        FakeChainSimulator::set_bootstrap_heights_override(0);

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
    if (!app_platform_create_window(&create_info, "Blockviz Bench Tests",
                                    static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height)))
    {
        std::printf("[bench] create window failed\n");
        FakeChainSimulator::set_bootstrap_heights_override(0);
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
    if (dense)
        apply_end_camera_down_z(camera, -48.f);
    else
    {
        camera.set_scroll_z(-48.f);
        camera.add_look_delta(0.f, 180.f);
        for (int i = 0; i < 30; ++i)
            camera.tick(1.f / 30.f);
    }

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
    engine->enable_frame_profiler(true);

    engine->start();

    app_platform_show_window(create_info.window);

    std::printf("[bench] case=%s warmup_ms=%d samples=%d out=%s dense=%d\n",
                case_id.c_str(), warmup_ms, samples, out_path.c_str(), dense ? 1 : 0);
    if (dense)
    {
        const int want = kDenseBootstrapHeights * BlockScene::kLaneCount;
        wait_min_blocks(scene, want / 2, warmup_ms);
        std::printf("[bench] total_blocks=%d (want~%d)\n", scene.total_blocks(), want);
    }
    pump_for_ms(warmup_ms);

    if (dense)
        apply_end_camera_down_z(camera, -48.f);
    else
    {
        camera.set_scroll_z(-48.f);
        for (int i = 0; i < 10; ++i)
            camera.tick(1.f / 30.f);
    }
    pump_for_ms(400);

    if (bfs)
    {
        const std::string sel = pick_selection_hash(scene);
        if (!sel.empty())
        {
            std::printf("[bench] bfs selection=%.20s…\n", sel.c_str());
            engine->set_selection(sel);
            pump_for_ms(600);
        }
        else
            std::printf("[bench] WARN: no selection hash for fake_bfs_end_z\n");
    }

    std::vector<float> frame_ms;
    std::vector<float> cpu_ms;
    std::vector<float> gpu_ms;
    frame_ms.reserve(static_cast<size_t>(samples));
    cpu_ms.reserve(static_cast<size_t>(samples));
    gpu_ms.reserve(static_cast<size_t>(samples));

    struct ScopeSeries
    {
        std::string name;
        std::vector<float> cpu;
        std::vector<float> gpu;
    };
    std::vector<ScopeSeries> series;

    uint64_t last_idx = 0;
    int got = 0;
    int spins = 0;
    const int max_spins = samples * 8 + 200;
    float move_scroll = -48.f;
    while (got < samples && spins < max_spins && app_platform_is_running())
    {
        if (moving)
        {
            // Oscillate along timeline Z to re-sort opaques each frame.
            move_scroll = -48.f + 40.f * std::sin(static_cast<float>(got) * 0.08f);
            camera.set_scroll_z(move_scroll);
            camera.tick(1.f / 60.f);
        }
        pump_for_ms(sample_ms);
        ++spins;

        FrameTimingSnapshot snap{};
        engine->copy_frame_timing_snapshot(snap);
        if (!snap.valid || snap.sample_index == 0)
            continue;
        if (snap.sample_index == last_idx)
            continue;
        last_idx = snap.sample_index;

        frame_ms.push_back(snap.frame_ms);
        cpu_ms.push_back(snap.cpu_ms);
        gpu_ms.push_back(snap.gpu_ms);

        for (uint32_t i = 0; i < snap.scope_count; ++i)
        {
            const FrameTimingScope& sc = snap.scopes[i];
            ScopeSeries* slot = nullptr;
            for (ScopeSeries& s : series)
            {
                if (s.name == sc.name)
                {
                    slot = &s;
                    break;
                }
            }
            if (!slot)
            {
                series.push_back(ScopeSeries{ sc.name, {}, {} });
                slot = &series.back();
            }
            slot->cpu.push_back(sc.cpu_ms);
            slot->gpu.push_back(sc.gpu_ms);
        }
        ++got;
    }

    engine->stop();
    engine->free_systems();
    destroy_engine(engine);
    g_engine = nullptr;
    delete presenter;
    app_platform_destroy_window(create_info.window);
    FakeChainSimulator::set_bootstrap_heights_override(0);

    if (got < 8)
    {
        std::printf("[bench] too few samples (%d)\n", got);
        return 1;
    }

    const float frame_med = percentile(frame_ms, 0.50f);
    const float frame_p95 = percentile(frame_ms, 0.95f);
    const float cpu_med = percentile(cpu_ms, 0.50f);
    const float cpu_p95 = percentile(cpu_ms, 0.95f);
    const float gpu_med = percentile(gpu_ms, 0.50f);
    const float gpu_p95 = percentile(gpu_ms, 0.95f);

    FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f)
    {
        std::printf("[bench] failed to open %s\n", out_path.c_str());
        return 1;
    }

    // Soft budgets (ms) for confidence: score 1 if median ≤ budget, decays over soft_tol band.
    // Tuned as "good enough" on a modern discrete GPU; not absolute cross-device guarantees.
    const float soft_tol = 0.40f; // 40% over budget → score 0
    auto metric_score = [soft_tol](float median, float budget) -> float {
        if (budget <= 0.f)
            return 1.f;
        if (median <= budget)
            return 1.f;
        const float over = (median - budget) / (budget * soft_tol);
        if (over >= 1.f)
            return 0.f;
        return 1.f - over;
    };
    // Budgets: steady tighter; stress/dense allow higher frame time.
    float budget_frame = 8.0f;
    float budget_cpu = 4.0f;
    float budget_gpu = 5.0f;
    if (case_id == "fake_stress_instances")
    {
        budget_frame = 12.0f;
        budget_cpu = 6.0f;
        budget_gpu = 8.0f;
    }
    else if (case_id == "fake_overdraw_end_z")
    {
        // Dense Prepare (F2B sort + layout) dominates CPU on discrete GPUs.
        budget_frame = 28.0f;
        budget_cpu = 26.0f;
        budget_gpu = 2.0f;
    }
    else if (case_id == "fake_overdraw_end_z_move")
    {
        budget_frame = 30.0f;
        budget_cpu = 28.0f;
        budget_gpu = 2.0f;
    }
    else if (case_id == "fake_bfs_end_z")
    {
        budget_frame = 30.0f;
        budget_cpu = 28.0f;
        budget_gpu = 3.0f;
    }
    const float s_frame = metric_score(frame_med, budget_frame);
    const float s_cpu = metric_score(cpu_med, budget_cpu);
    const float s_gpu = metric_score(gpu_med, budget_gpu);
    // Weighted confidence (frame dominates).
    const float confidence =
        (0.5f * s_frame + 0.25f * s_cpu + 0.25f * s_gpu);

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"case\": \"%s\",\n", case_id.c_str());
    std::fprintf(f, "  \"samples\": %d,\n", got);
    std::fprintf(f, "  \"confidence\": %.4f,\n", confidence);
    std::fprintf(f, "  \"scores\": { \"frame\": %.4f, \"cpu\": %.4f, \"gpu\": %.4f },\n",
                 s_frame, s_cpu, s_gpu);
    std::fprintf(f,
                 "  \"budgets_ms\": { \"frame\": %.2f, \"cpu\": %.2f, \"gpu\": %.2f, "
                 "\"soft_tol\": %.2f },\n",
                 budget_frame, budget_cpu, budget_gpu, soft_tol);
    std::fprintf(f, "  \"frame_ms\": { \"median\": %.4f, \"p95\": %.4f },\n", frame_med, frame_p95);
    std::fprintf(f, "  \"cpu_ms\": { \"median\": %.4f, \"p95\": %.4f },\n", cpu_med, cpu_p95);
    std::fprintf(f, "  \"gpu_ms\": { \"median\": %.4f, \"p95\": %.4f },\n", gpu_med, gpu_p95);
    std::fprintf(f, "  \"scopes\": {\n");
    for (size_t i = 0; i < series.size(); ++i)
    {
        const ScopeSeries& s = series[i];
        const float cmed = percentile(s.cpu, 0.50f);
        const float cp95 = percentile(s.cpu, 0.95f);
        const float gmed = percentile(s.gpu, 0.50f);
        const float gp95 = percentile(s.gpu, 0.95f);
        std::fprintf(f,
                     "    \"%s\": { \"cpu_median\": %.4f, \"cpu_p95\": %.4f, \"gpu_median\": %.4f, \"gpu_p95\": %.4f }%s\n",
                     s.name.c_str(), cmed, cp95, gmed, gp95,
                     (i + 1 < series.size()) ? "," : "");
    }
    std::fprintf(f, "  }\n");
    std::fprintf(f, "}\n");
    std::fclose(f);

    std::printf("[bench] wrote %s (samples=%d frame_med=%.3f cpu_med=%.3f gpu_med=%.3f "
                "confidence=%.3f)\n",
                out_path.c_str(), got, frame_med, cpu_med, gpu_med, confidence);
    // Serious regression only: confidence < 0.4 fails harness exit (opt-in CI).
    if (confidence < 0.4f)
    {
        std::printf("[bench] FAIL confidence %.3f < 0.4 (serious regression vs soft budgets)\n",
                    confidence);
        return 3;
    }
    if (confidence < 0.7f)
        std::printf("[bench] WARN confidence %.3f < 0.7 (soft budget headroom low)\n",
                    confidence);
    return 0;
}
