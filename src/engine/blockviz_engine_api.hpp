#pragma once

// Public host/adapter surface for the block visualizer engine (E6/E7).
// No Vulkan headers. Concrete VulkanEngine is engine-lib internal.
// See graphics/gpu_pub_lib.h for IRenderEngine base.

#include "alph_block.hpp"
#include "app/ui_snapshot.hpp"
#include "graphics/camera.hpp"
#include "graphics/gpu_pub_lib.h"

#include <string>
#include <vector>

#include <glm/glm.hpp>

class BlockScene;
class CameraController;
class DebugDrawer; // graphics debug draw; filled on render thread (E14)

// Selection/hover snapshot the engine passes into the host frame builder (E14).
struct FrameSourceInput
{
    std::string selected_hash;
    std::string hovered_hash;
    AlphBlock   selected_detail;
    // Optional frustum cull before instance submit (null = no cull).
    const Frustum* frustum = nullptr;
    // World half-extents of each cube instance (mesh vertices at ±1 → 1,1,1).
    glm::vec3 instance_half_extents{ 1.f, 1.f, 1.f };
};

// Domain → GPU/UI products built on the render thread by the host (E14).
struct FrameSourceOutput
{
    std::vector<GpuInstance> instances;
    std::vector<std::string> pick_map;
    bool      has_look_target = false;
    glm::vec3 look_target_pos{ 0.f };
    UiSnapshot ui{};

    // Confirmed tip hashes still present in this frame's pick_map (frustum-culled out = omitted).
    // Engine maps hash → instance index for multi-draw Sobel depth.
    std::vector<std::string> confirmed_tip_hashes;
};

// Host implements; engine calls prepare() each frame on the render thread.
// prepare may lock BlockScene; engine must not hold scene mutex across the call.
class IFrameSource
{
public:
    virtual ~IFrameSource() = default;
    // debug may be null; when non-null it is already cleared for this frame.
    virtual void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                         DebugDrawer* debug) = 0;
};

// App + network selection / domain wiring beyond pure IRenderEngine.
class IBlockvizEngine : public IRenderEngine
{
public:
    ~IBlockvizEngine() override = default;

    virtual void set_scene(BlockScene* scene) = 0;
    virtual void set_camera(CameraController* camera) = 0;
    virtual void set_frame_source(IFrameSource* source) = 0; // not owned; nullptr = empty scene path

    virtual void set_selection(const std::string& hash) = 0;
    virtual void clear_selection() = 0;
    virtual bool is_selected(const std::string& hash) const = 0;
    virtual AlphBlock copy_selected_block() const = 0;
    virtual std::string consume_detail_refill_request() = 0;

    virtual void publish_ui_snapshot(UiSnapshot snap) = 0;
    virtual UiSnapshot copy_ui_snapshot() const = 0;

    // Render-loop path: instances + pick map + confirmed tips (extends IRenderEngine::submit_frame)
    virtual void publish_frame(const FrameSubmit& frame,
                               const std::vector<std::string>& pick_map,
                               const std::vector<std::string>& confirmed_tip_hashes) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;
};

IBlockvizEngine* create_blockviz_engine();
void destroy_blockviz_engine(IBlockvizEngine* engine);
