#pragma once

// Product engine facade (engine.lib). No Vulkan / curl.
// BlockVizEngine registers ISystem components and lifecycle them via init/free.
// Prefer narrow includes: engine/frame_types.hpp, engine/system.hpp when possible.
// Living docs: docs/layers/engine.md (map: docs/layers/README.md).

#include "domain/alph_block.hpp"
#include "app/ui_snapshot.hpp"
#include "engine/system.hpp"
#include "engine/frame_types.hpp"
#include "graphics/camera.hpp"
#include "graphics/gpu_pub_lib.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class BlockScene;
class CameraController;

// ---------------------------------------------------------------------------
// Network system config + interface
// ---------------------------------------------------------------------------

struct NetworkSystemConfig
{
    std::string base_url;
    int64_t     lookback_ms      = 0;
    int64_t     poll_interval_ms = 8000;
    int         domain           = 0; // NetworkDomain as int
};

class INetworkSystem : public ISystem
{
public:
    ~INetworkSystem() override = default;
    // Store config before polymorphic init().
    virtual void configure(const NetworkSystemConfig& cfg) = 0;

    // Hot-switch Mainnet/Testnet/Debug. Resets scene + restarts poller or FakeChain.
    // Safe to call from UI/render thread; blocks until backend restarted.
    // Debug may pass empty base_url (resolved to debug://fake-chain).
    virtual bool switch_domain(int domain, const std::string& base_url) = 0;
    virtual int  domain() const = 0;
    virtual bool is_switching() const = 0;
    virtual std::string base_url() const = 0;
};

// ---------------------------------------------------------------------------
// Graphics system interface (graphics.lib implements)
// ---------------------------------------------------------------------------

class IGraphicsSystem : public ISystem
{
public:
    ~IGraphicsSystem() override = default;

    // Store create info before polymorphic init().
    virtual void configure(const EngineCreateInfo& info) = 0;

    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void submit_frame(const FrameSubmit& frame) = 0;
    virtual void set_ui_overlay(IUiOverlay* overlay) = 0;
    virtual void request_pick(const PickQuery& q) = 0;
    virtual bool consume_pick(PickResult& out) = 0;

    virtual void set_scene(BlockScene* scene) = 0;
    virtual void set_camera(CameraController* camera) = 0;
    virtual void set_frame_source(IFrameSource* source) = 0;

    virtual void set_selection(const std::string& hash) = 0;
    virtual void clear_selection() = 0;
    virtual bool is_selected(const std::string& hash) const = 0;
    virtual AlphBlock copy_selected_block() const = 0;
    // Inspector Deps list hover (empty clears); used to recolor selection arrows.
    virtual void set_ui_dep_hover(const std::string& hash) = 0;
    // Scene view filter: only blocks with txn_count > 1.
    virtual void set_scene_filter_multi_tx(bool enabled) = 0;
    // Min block total output ALPH (human units, e.g. 1.5); 0 = off.
    virtual void set_scene_filter_min_alph(double min_alph) = 0;
    // Client-area PNG capture (includes ImGui). Empty path auto-names under docs/images/.
    virtual void request_screenshot(const char* path_utf8) = 0;
    virtual std::string consume_detail_refill_request() = 0;

    virtual void publish_ui_snapshot(UiSnapshot snap) = 0;
    virtual UiSnapshot copy_ui_snapshot() const = 0;

    virtual void publish_frame(const FrameSubmit& frame,
                               const std::vector<std::string>& pick_map,
                               const std::vector<std::string>& confirmed_tip_hashes,
                               const std::vector<std::string>& cyan_frontier_hashes,
                               const std::vector<std::string>& incomplete_hashes) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;
};

// ---------------------------------------------------------------------------
// Product engine interface
// ---------------------------------------------------------------------------

class IEngine
{
public:
    virtual ~IEngine() = default;

    // --- ISystem registry (engine takes ownership of added systems) ---
    virtual void add_system(ISystem* system) = 0;
    virtual ISystem* find_system(const char* name) const = 0;

    // Polymorphic lifecycle for all registered systems (registration order / reverse).
    virtual void init_systems() = 0;
    virtual void free_systems() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    // --- Product API (forwarded to registered GraphicsSystem) ---
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void submit_frame(const FrameSubmit& frame) = 0;
    virtual void set_ui_overlay(IUiOverlay* overlay) = 0;
    virtual void request_pick(const PickQuery& q) = 0;
    virtual bool consume_pick(PickResult& out) = 0;

    virtual void set_scene(BlockScene* scene) = 0;
    virtual void set_camera(CameraController* camera) = 0;
    virtual void set_frame_source(IFrameSource* source) = 0;

    virtual void set_selection(const std::string& hash) = 0;
    virtual void clear_selection() = 0;
    virtual bool is_selected(const std::string& hash) const = 0;
    virtual AlphBlock copy_selected_block() const = 0;
    virtual void set_ui_dep_hover(const std::string& hash) = 0;
    virtual void set_scene_filter_multi_tx(bool enabled) = 0;
    virtual void set_scene_filter_min_alph(double min_alph) = 0;
    virtual void request_screenshot(const char* path_utf8) = 0;
    virtual std::string consume_detail_refill_request() = 0;

    virtual void publish_ui_snapshot(UiSnapshot snap) = 0;
    virtual UiSnapshot copy_ui_snapshot() const = 0;

    virtual void publish_frame(const FrameSubmit& frame,
                               const std::vector<std::string>& pick_map,
                               const std::vector<std::string>& confirmed_tip_hashes,
                               const std::vector<std::string>& cyan_frontier_hashes,
                               const std::vector<std::string>& incomplete_hashes) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;

    // Convenience: find NetworkSystem and switch domain (no-op if missing).
    virtual bool switch_network_domain(int domain, const std::string& base_url) = 0;
    virtual int  network_domain() const = 0;
    virtual bool network_is_switching() const = 0;
};

// engine.lib — concrete type is BlockVizEngine
IEngine* create_engine();
void destroy_engine(IEngine* engine);

// graphics.lib
IGraphicsSystem* create_graphics_system();
void destroy_graphics_system(IGraphicsSystem* graphics);

// network.lib
INetworkSystem* create_network_system(BlockScene& scene, IEngine& engine);
void destroy_network_system(INetworkSystem* net);
