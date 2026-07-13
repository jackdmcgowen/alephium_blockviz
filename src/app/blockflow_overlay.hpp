#pragma once

// App ImGui chrome: toolbar + block inspector (PR8 / Spike S4).
// Explorer.alephium.org URLs live only in this TU.
#include "app/camera_controller.hpp"
#include "app/ui_snapshot.hpp"
#include "engine/blockviz_engine_api.hpp"
#include "graphics/gpu_pub_lib.h"

#include <cstdint>

class BlockflowOverlay : public IUiOverlay
{
public:
    BlockflowOverlay(CameraController& camera, IBlockvizEngine& engine);

    void draw() override; // render thread; ImGui only

    void set_session_start_ms(int64_t start_ms) { session_start_ms_ = start_ms; }

private:
    void draw_toolbar(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h);

    CameraController& camera_;
    IBlockvizEngine&  engine_;
    int64_t           session_start_ms_ = 0;

    // RMB pan: drag vs short-click reset (clear selection, home look + pan origin)
    bool  rmb_down_over_scene_ = false;
    bool  rmb_dragged_         = false;
    float rmb_drag_dist_px_    = 0.f;

    // LMB look: drag vs short-click pick (engine uses MouseDragMaxDistance)
    bool  lmb_down_over_scene_ = false;
    bool  lmb_dragged_         = false;
    float lmb_drag_dist_px_    = 0.f;
};
