#pragma once

// Product engine facade (engine.lib). No Vulkan / curl.
// Engine shell owns GraphicsSystem + NetworkSystem (ISystem derivatives).

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

    std::vector<std::string> confirmed_tip_hashes;
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
    virtual void init(const NetworkSystemConfig& cfg) = 0;
};

// ---------------------------------------------------------------------------
// Graphics system interface (graphics.lib implements)
// ---------------------------------------------------------------------------

class IGraphicsSystem : public ISystem
{
public:
    ~IGraphicsSystem() override = default;

    virtual void init(const EngineCreateInfo& info) = 0;
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
                               const std::vector<std::string>& incomplete_hashes) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;
};

// ---------------------------------------------------------------------------
// Product engine
// ---------------------------------------------------------------------------

class IEngine
{
public:
    virtual ~IEngine() = default;

    // Create+init graphics, then (optionally later) network.
    virtual void init(const EngineCreateInfo& info) = 0;
    virtual void init(const NetworkSystemConfig& cfg) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

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
                               const std::vector<std::string>& incomplete_hashes) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;
};

// engine.lib
IEngine* create_engine();
void destroy_engine(IEngine* engine);

// graphics.lib — used by Engine shell only
IGraphicsSystem* create_graphics_system();
void destroy_graphics_system(IGraphicsSystem* graphics);

// network.lib — used by Engine shell only
INetworkSystem* create_network_system(BlockScene& scene, IEngine& engine);
void destroy_network_system(INetworkSystem* net);
