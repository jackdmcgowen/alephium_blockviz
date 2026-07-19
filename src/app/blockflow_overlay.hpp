#pragma once

// App ImGui chrome: Network (left, Blockflow collapsible) + Block (right).
#include "app/camera_controller.hpp"
#include "app/ui_snapshot.hpp"
#include "engine/engine.hpp"
#include "graphics/gpu_pub_lib.h"
#include "network/network_domain.hpp"

#include <cstdint>
#include <string>
#include <vector>

class BlockflowOverlay : public IUiOverlay
{
public:
    BlockflowOverlay(CameraController& camera, IEngine& engine);

    void draw() override; // render thread; ImGui only

    void set_session_start_ms(int64_t start_ms) { session_start_ms_ = start_ms; }

    // Optional config URLs for domain resolution (mainnet/testnet from config.json).
    void set_domain_urls(std::vector<std::string> urls);
    void set_initial_domain(NetworkDomain d);

private:
    void draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_network(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_block_billboard_(const UiSnapshot& ui, float ui_w, float ui_h, float dt_sec);
    void apply_domain_if_changed_();
    std::string resolve_url_(NetworkDomain d) const;

    CameraController& camera_;
    IEngine&  engine_;
    int64_t           session_start_ms_ = 0;

    NetworkDomain domain_ = NetworkDomain::Mainnet;
    std::vector<std::string> domain_urls_;

    // RMB pan: drag vs short-click reset (clear selection, home look + pan origin)
    bool  rmb_down_over_scene_ = false;
    bool  rmb_dragged_         = false;
    float rmb_drag_dist_px_    = 0.f;

    // LMB look: drag vs short-click pick (engine uses MouseDragMaxDistance)
    bool  lmb_down_over_scene_ = false;
    bool  lmb_dragged_         = false;
    float lmb_drag_dist_px_    = 0.f;

    // Hover billboard fade (overlay-owned alpha).
    static constexpr float kBillboardFadeInSec  = 0.18f;
    static constexpr float kBillboardFadeOutSec = 0.30f;
    float       billboard_alpha_ = 0.f;
    std::string billboard_hash_;
    float       billboard_pos_[3]{ 0.f, 0.f, 0.f };
    int         billboard_height_     = -1;
    int         billboard_chain_from_ = -1;
    int         billboard_chain_to_   = -1;
    int         billboard_txn_count_  = -1;
};
