#pragma once

// App ImGui chrome: toolbar + block inspector (PR8 / Spike S4).
// Explorer.alephium.org URLs live only in this TU.
#include "app/camera_state.hpp"
#include "app/ui_snapshot.hpp"
#include "graphics/gpu_pub_lib.h"

#include <cstdint>

class VulkanEngine;

class BlockflowOverlay : public IUiOverlay
{
public:
    BlockflowOverlay(CameraState& camera, VulkanEngine& engine);

    void draw() override; // render thread; ImGui only

    // Session start time for rate display (ms epoch). Set once from app if desired.
    void set_session_start_ms(int64_t start_ms) { session_start_ms_ = start_ms; }

private:
    void draw_toolbar(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h);

    CameraState&    camera_;
    VulkanEngine& engine_;
    int64_t         session_start_ms_ = 0;
};
