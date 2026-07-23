#include "app/pch.h"
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>

#include "domain/alph_block.hpp"
#include "app/app_identity.hpp"
#include "app/blockflow_overlay.hpp"
#include "app/camera_controller.hpp"
#include "app/scene_presenter.hpp"
#include "app/config.h"
#include "app/user_prefs.hpp"
#include "app/platform/app_platform.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"
#include "graphics/gpu_pub_lib.h"
#include "network/network_domain.hpp"

#include <string>
#include <vector>

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

static void on_resize(void* /*user*/)
{
    if (engine)
        engine->on_resize();
}

static void on_exit_request(void* /*user*/)
{
    // Stop systems while native window is still valid (platform hides first).
    stop_engine_once();
}

int main()
{
    EngineCreateInfo create_info{};
    create_info.application = app_identity::make();
    if (!app_platform_create_window(&create_info, app_identity::kName, WDW_WIDTH, WDW_HEIGHT))
        return -1;

    ConfigArray config_array = load_configs("config.json");
    if (config_array.count == 0 || !config_array.configs[0].url)
    {
        printf("Failed to load config\n");
        app_platform_destroy_window(create_info.window);
        return -1;
    }

    std::vector<std::string> domain_urls;
    for (int i = 0; i < config_array.count; ++i)
    {
        if (config_array.configs[i].url)
            domain_urls.emplace_back(config_array.configs[i].url);
    }
    const UserPrefs prefs = load_user_prefs();
    // Prefer last-used domain from user_prefs.json; fall back to first config URL.
    NetworkDomain boot_domain = prefs.domain;
    if (boot_domain != NetworkDomain::Debug && boot_domain != NetworkDomain::Mainnet &&
        boot_domain != NetworkDomain::Testnet)
        boot_domain = network_domain_from_url(config_array.configs[0].url);
    std::vector<const char*> url_ptrs;
    url_ptrs.reserve(domain_urls.size());
    for (const auto& u : domain_urls)
        url_ptrs.push_back(u.c_str());
    const std::string boot_url = network_domain_resolve_url(
        boot_domain, url_ptrs.empty() ? nullptr : url_ptrs.data(),
        static_cast<int>(url_ptrs.size()));
    printf("Using config url: %s (domain %s)\n", boot_url.c_str(),
           network_domain_label(boot_domain));
    if (prefs.filter_multi_tx)
        printf("Prefs: filter_multi_tx=on\n");

    engine = create_engine();

    IGraphicsSystem* graphics = create_graphics_system();
    graphics->configure(create_info);
    engine->add_system(graphics);

    NetworkSystemConfig net_cfg;
    net_cfg.base_url = boot_url;
    net_cfg.domain = static_cast<int>(boot_domain);
    const int lookback_sec = (prefs.lookback_seconds > 0)
                                 ? prefs.lookback_seconds
                                 : ALPH_LOOKBACK_WINDOW_SECONDS;
    net_cfg.lookback_ms = static_cast<int64_t>(lookback_sec) * 1000;
    net_cfg.poll_interval_ms = static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;

    INetworkSystem* network = create_network_system(scene, *engine);
    network->configure(net_cfg);
    engine->add_system(network);

    engine->init_systems();

    overlay = new BlockflowOverlay(camera, *engine);
    overlay->set_domain_urls(domain_urls);
    overlay->set_initial_domain(boot_domain);
    if (prefs.filter_multi_tx)
        overlay->set_filter_multi_tx(true);
    if (prefs.filter_min_alph > 0.0)
        overlay->set_filter_min_alph(prefs.filter_min_alph);
    if (prefs.filter_unconfirmed_only)
        overlay->set_filter_unconfirmed_only(true);
    scene_presenter = new ScenePresenter(scene);
    overlay->set_block_scene(&scene);
    engine->set_scene(&scene);
    engine->set_camera(&camera);
    engine->set_frame_source(scene_presenter);
    engine->set_ui_overlay(overlay);

    engine->start();

    app_platform_show_window(create_info.window);

    AppPlatformCallbacks cb{};
    cb.on_resize = &on_resize;
    cb.on_exit_request = &on_exit_request;
    app_platform_run_loop(cb);

    stop_engine_once();
    if (overlay)
        overlay->save_prefs();
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
