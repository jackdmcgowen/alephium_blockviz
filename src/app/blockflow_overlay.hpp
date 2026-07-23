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

class BlockScene;

class BlockflowOverlay : public IUiOverlay
{
public:
    BlockflowOverlay(CameraController& camera, IEngine& engine);

    void draw() override; // render thread; ImGui only

    void set_session_start_ms(int64_t start_ms) { session_start_ms_ = start_ms; }
    void set_block_scene(BlockScene* scene) { scene_ = scene; }

    // Optional config URLs for domain resolution (mainnet/testnet from config.json).
    void set_domain_urls(std::vector<std::string> urls);
    void set_initial_domain(NetworkDomain d);
    void set_filter_multi_tx(bool enabled);
    bool filter_multi_tx() const { return filter_multi_tx_; }
    void set_filter_min_alph(double min_alph);
    double filter_min_alph() const { return filter_min_alph_; }
    void set_filter_unconfirmed_only(bool enabled);
    bool filter_unconfirmed_only() const { return filter_unconfirmed_only_; }

    // Persist domain + filter to user_prefs.json (best-effort).
    void save_prefs() const;

private:
    void push_scene_view_filters_();
    void draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_network(const UiSnapshot& ui, float ui_w, float ui_h);
    void draw_block_billboard_(const UiSnapshot& ui, float ui_w, float ui_h, float dt_sec);
    void draw_timeline_minimap_(const UiSnapshot& ui, float ui_w, float ui_h);
    void apply_domain_if_changed_();
    std::string resolve_url_(NetworkDomain d) const;
    float ts_to_z_(int64_t ts_ms, int64_t origin_ms, float mps) const;

    CameraController& camera_;
    IEngine&  engine_;
    BlockScene*       scene_ = nullptr;
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
    bool        billboard_is_uncle_   = false;
    char        billboard_alph_[48]{};

    // Scene view: only draw blocks with txn_count > 1.
    bool        filter_multi_tx_ = false;
    // Min block output ALPH (human); 0 = off.
    double      filter_min_alph_ = 0.0;
    bool        filter_unconfirmed_only_ = false;
    // Minimap scrub state.
    bool        minimap_dragging_ = false;
    // Edge page rate-limit (ImGui time seconds); allows hold-to-page, not one-shot lock.
    double      minimap_edge_page_next_sec_ = 0.0;
};
