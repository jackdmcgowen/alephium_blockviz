#pragma once

// Product engine facade (engine.lib). No Vulkan / curl.
// BlockVizEngine registers ISystem components and lifecycle them via init/free.

#include "domain/alph_block.hpp"
#include "app/ui_snapshot.hpp"
#include "engine/system.hpp"
#include "graphics/camera.hpp"
#include "graphics/gpu_pub_lib.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class BlockScene;
class CameraController;
class DebugDrawer;

// ---------------------------------------------------------------------------
// Frame source (host implements; graphics render thread calls prepare)
// ---------------------------------------------------------------------------

struct FrameSourceInput
{
    std::string selected_hash;
    std::string hovered_hash;
    AlphBlock   selected_detail;
    const Frustum* frustum = nullptr;
    glm::vec3 instance_half_extents{ 1.f, 1.f, 1.f };
};

struct FrameSourceOutput
{
    std::vector<GpuInstance> instances;
    std::vector<std::string> pick_map;
    bool      has_look_target = false;
    glm::vec3 look_target_pos{ 0.f };
    UiSnapshot ui{};

    // Green Sobel: confirmed frontier tip H_c per lane (walk-anim display may lag).
    std::vector<std::string> confirmed_tip_hashes;
    // Cyan Sobel: frontier children — unconfirmed height>H_c that deps a frontier tip.
    std::vector<std::string> cyan_frontier_hashes;
    // Orange Sobel: missing-dep incompletes (excluding green/cyan).
    std::vector<std::string> incomplete_hashes;
};

class IFrameSource
{
public:
    virtual ~IFrameSource() = default;
    virtual void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                         DebugDrawer* debug) = 0;
};

// ---------------------------------------------------------------------------
// Network system config + interface
// ---------------------------------------------------------------------------

struct NetworkSystemConfig
{
    std::string base_url;
    int64_t     lookback_ms      = 0;
    int64_t     poll_interval_ms = 8000;
};

class INetworkSystem : public ISystem
{
public:
    ~INetworkSystem() override = default;
    // Store config before polymorphic init().
    virtual void configure(const NetworkSystemConfig& cfg) = 0;
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

// engine.lib — concrete type is BlockVizEngine
IEngine* create_engine();
void destroy_engine(IEngine* engine);

// graphics.lib
IGraphicsSystem* create_graphics_system();
void destroy_graphics_system(IGraphicsSystem* graphics);

// network.lib
INetworkSystem* create_network_system(BlockScene& scene, IEngine& engine);
void destroy_network_system(INetworkSystem* net);
