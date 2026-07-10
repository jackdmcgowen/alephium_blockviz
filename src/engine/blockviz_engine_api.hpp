#pragma once

// Public host/adapter surface for the block visualizer engine (E6/E7).
// No Vulkan headers. Concrete VulkanEngine is engine-lib internal.
// See graphics/gpu_pub_lib.h for IRenderEngine base.

#include "alph_block.hpp"
#include "app/ui_snapshot.hpp"
#include "graphics/gpu_pub_lib.h"

#include <string>
#include <vector>

class BlockScene;
class CameraState;

// App + network selection / domain wiring beyond pure IRenderEngine.
class IBlockvizEngine : public IRenderEngine
{
public:
    ~IBlockvizEngine() override = default;

    virtual void set_scene(BlockScene* scene) = 0;
    virtual void set_camera(CameraState* camera) = 0;

    virtual void set_selection(const std::string& hash) = 0;
    virtual void clear_selection() = 0;
    virtual bool is_selected(const std::string& hash) const = 0;
    virtual AlphBlock copy_selected_block() const = 0;
    virtual std::string consume_detail_refill_request() = 0;

    virtual void publish_ui_snapshot(UiSnapshot snap) = 0;
    virtual UiSnapshot copy_ui_snapshot() const = 0;

    // Render-loop path: instances + pick map (extends IRenderEngine::submit_frame)
    virtual void publish_frame(const FrameSubmit& frame,
                               const std::vector<std::string>& pick_map) = 0;

    virtual void init_platform(void* hInstance, void* hwnd) = 0;
    virtual void on_resize() = 0;
};

IBlockvizEngine* create_blockviz_engine();
void destroy_blockviz_engine(IBlockvizEngine* engine);
