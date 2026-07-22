#include "app/pch.h"
#include "app/blockflow_overlay.hpp"
#include "app/user_prefs.hpp"
#include "app/ui_chrome.hpp"
#include "domain/block_scene.hpp"
#include "network/network_domain.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

#include <glm/glm.hpp>

// Shard colors for feed / inspector labels (UX only â€” not engine)
static const glm::vec3 kShardColors[16] = {
    glm::vec3(1.00f, 0.34f, 0.20f),
    glm::vec3(0.20f, 1.00f, 0.34f),
    glm::vec3(0.20f, 0.34f, 1.00f),
    glm::vec3(1.00f, 0.20f, 1.00f),
    glm::vec3(1.00f, 0.76f, 0.00f),
    glm::vec3(0.85f, 0.97f, 0.65f),
    glm::vec3(0.78f, 0.00f, 0.22f),
    glm::vec3(0.34f, 0.09f, 0.27f),
    glm::vec3(1.00f, 1.00f, 1.00f),
    glm::vec3(0.50f, 0.50f, 0.00f),
    glm::vec3(0.00f, 1.00f, 1.00f),
    glm::vec3(1.00f, 0.75f, 0.80f),
    glm::vec3(0.50f, 0.00f, 0.50f),
    glm::vec3(1.00f, 1.00f, 0.00f),
    glm::vec3(0.50f, 0.50f, 0.50f),
    glm::vec3(0.00f, 1.00f, 0.00f)
};

BlockflowOverlay::BlockflowOverlay(CameraController& camera, IEngine& engine)
    : camera_(camera)
    , engine_(engine)
{
    session_start_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
}

void BlockflowOverlay::set_domain_urls(std::vector<std::string> urls)
{
    domain_urls_ = std::move(urls);
}

void BlockflowOverlay::set_initial_domain(NetworkDomain d)
{
    domain_ = d;
}

void BlockflowOverlay::set_filter_multi_tx(bool enabled)
{
    filter_multi_tx_ = enabled;
    engine_.set_scene_filter_multi_tx(filter_multi_tx_);
}

void BlockflowOverlay::set_filter_min_alph(double min_alph)
{
    filter_min_alph_ = (min_alph > 0.0) ? min_alph : 0.0;
    engine_.set_scene_filter_min_alph(filter_min_alph_);
}

void BlockflowOverlay::save_prefs() const
{
    UserPrefs p;
    p.domain = domain_;
    p.filter_multi_tx = filter_multi_tx_;
    p.filter_min_alph = filter_min_alph_;
    if (!save_user_prefs(p))
        std::printf("[app] warning: failed to save %s\n", kUserPrefsPath);
}

float BlockflowOverlay::ts_to_z_(int64_t ts_ms, int64_t origin_ms, float mps) const
{
    return -static_cast<float>(ts_ms - origin_ms) * 0.001f * mps;
}

std::string BlockflowOverlay::resolve_url_(NetworkDomain d) const
{
    std::vector<const char*> ptrs;
    ptrs.reserve(domain_urls_.size());
    for (const auto& u : domain_urls_)
        ptrs.push_back(u.c_str());
    return network_domain_resolve_url(d, ptrs.empty() ? nullptr : ptrs.data(),
                                      static_cast<int>(ptrs.size()));
}

void BlockflowOverlay::apply_domain_if_changed_()
{
    const int eng = engine_.network_domain();
    if (eng == static_cast<int>(domain_))
        return;
    // Sync from engine if something else switched (should be rare).
    if (domain_ != NetworkDomain::Debug)
        domain_ = static_cast<NetworkDomain>(eng);
}

void BlockflowOverlay::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt_sec = (io.DeltaTime > 0.f) ? io.DeltaTime : (1.f / 60.f);

    const float ui_w = io.DisplaySize.x;
    const float ui_h = io.DisplaySize.y;
    const float rail_w = ui_chrome::rail_width(ui_w);
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    // Full-height center band between Network and Block rails.
    const bool over_scene =
        !io.WantCaptureMouse &&
        mx >= rail_w && my >= 0.f &&
        mx < ui_w - rail_w &&
        my < ui_h;

    // Camera: L/R = Z always; End U/D = Z; Side U/D = orbit around chain.
    // Presets: 1=End, 2=Side, V=toggle.
    if (!io.WantCaptureKeyboard)
    {
        const bool side =
            camera_.view_preset() == CameraController::ViewPreset::Side;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))
            camera_.nudge_scroll(CameraController::kEyeZStep * dt_sec);
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow))
            camera_.nudge_scroll(-CameraController::kEyeZStep * dt_sec);
        if (side)
        {
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
                camera_.nudge_orbit(CameraController::kOrbitRadPerSec * dt_sec);
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
                camera_.nudge_orbit(-CameraController::kOrbitRadPerSec * dt_sec);
        }
        else
        {
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
                camera_.nudge_scroll(CameraController::kEyeZStep * dt_sec);
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
                camera_.nudge_scroll(-CameraController::kEyeZStep * dt_sec);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
            camera_.set_view_preset(CameraController::ViewPreset::End);
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
            camera_.set_view_preset(CameraController::ViewPreset::Side);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            camera_.toggle_view_preset();
        if (ImGui::IsKeyPressed(ImGuiKey_3, false))
            camera_.reattach_timeline(); // live chain only
        if (ImGui::IsKeyPressed(ImGuiKey_R, false) && scene_)
            scene_->request_walk_replay();
    }
    // Positive MouseWheel = scroll up → +scroll_z (timeline).
    if (over_scene && io.MouseWheel != 0.f)
        camera_.nudge_scroll(io.MouseWheel * CameraController::kWheelStep);

    constexpr float kDragThresholdPx = 4.f;

    // Left-hold drag: free look. Short LMB click still picks (engine).
    if (over_scene && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        lmb_down_over_scene_ = true;
        lmb_dragged_ = false;
        lmb_drag_dist_px_ = 0.f;
    }
    if (lmb_down_over_scene_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x;
        const float dy = io.MouseDelta.y;
        lmb_drag_dist_px_ += std::sqrt(dx * dx + dy * dy);
        if (lmb_drag_dist_px_ >= kDragThresholdPx)
            lmb_dragged_ = true;
        if (lmb_dragged_ && (dx != 0.f || dy != 0.f))
            camera_.add_look_delta(dx, dy);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        lmb_down_over_scene_ = false;
        lmb_dragged_ = false;
        lmb_drag_dist_px_ = 0.f;
    }

    // Right-hold drag: pan. Short RMB: deselect (if any) + home look/pan keep Z (always).
    if (over_scene && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        rmb_down_over_scene_ = true;
        rmb_dragged_ = false;
        rmb_drag_dist_px_ = 0.f;
    }
    if (rmb_down_over_scene_ && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x;
        const float dy = io.MouseDelta.y;
        rmb_drag_dist_px_ += std::sqrt(dx * dx + dy * dy);
        if (rmb_drag_dist_px_ >= kDragThresholdPx)
            rmb_dragged_ = true;
        if (rmb_dragged_ && (dx != 0.f || dy != 0.f))
            camera_.add_pan_delta(dx, dy);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (rmb_down_over_scene_ && !rmb_dragged_)
        {
            engine_.clear_selection(); // no Live reattach — key 3 / Live for tip
            camera_.home_view_keep_z(); // tween look +Z / pan XY→0; keep scroll Z
        }
        rmb_down_over_scene_ = false;
        rmb_dragged_ = false;
        rmb_drag_dist_px_ = 0.f;
    }

    const UiSnapshot ui = engine_.copy_ui_snapshot();
    apply_domain_if_changed_();

    draw_network(ui, ui_w, ui_h);
    draw_inspector(ui, ui_w, ui_h);
    draw_timeline_minimap_(ui, ui_w, ui_h);
    draw_block_billboard_(ui, ui_w, ui_h, dt_sec);
}

void BlockflowOverlay::draw_block_billboard_(const UiSnapshot& ui, float ui_w, float ui_h,
                                             float dt_sec)
{
    const auto& src = ui.block_billboard;
    const bool want = src.want_visible && src.hash[0] != '\0';

    // Latch content while fading out so text does not pop empty mid-fade.
    if (want)
    {
        billboard_hash_ = src.hash;
        billboard_pos_[0] = src.world_pos[0];
        billboard_pos_[1] = src.world_pos[1];
        billboard_pos_[2] = src.world_pos[2];
        billboard_height_ = src.height;
        billboard_chain_from_ = src.chain_from;
        billboard_chain_to_ = src.chain_to;
        billboard_txn_count_ = src.txn_count;
        billboard_is_uncle_ = src.is_uncle != 0;
        if (src.alph_out[0] != '\0')
            std::snprintf(billboard_alph_, sizeof(billboard_alph_), "%s", src.alph_out);
        else
            billboard_alph_[0] = '\0';
    }

    const float target = want ? 1.f : 0.f;
    if (target > billboard_alpha_)
    {
        const float rate = 1.f / std::max(kBillboardFadeInSec, 1e-3f);
        billboard_alpha_ = std::min(1.f, billboard_alpha_ + rate * dt_sec);
    }
    else if (target < billboard_alpha_)
    {
        const float rate = 1.f / std::max(kBillboardFadeOutSec, 1e-3f);
        billboard_alpha_ = std::max(0.f, billboard_alpha_ - rate * dt_sec);
    }

    if (billboard_alpha_ < 0.01f)
    {
        if (!want)
            billboard_hash_.clear();
        return;
    }

    const glm::vec3 world(billboard_pos_[0], billboard_pos_[1], billboard_pos_[2]);
    const glm::mat4 vp = camera_.camera().view_proj();
    const glm::vec4 clip = vp * glm::vec4(world, 1.f);
    if (clip.w <= 1e-4f)
        return;

    const float inv_w = 1.f / clip.w;
    const float ndc_x = clip.x * inv_w;
    const float ndc_y = clip.y * inv_w;
    // Camera up is (0,-1,0); NDC y already matches top-down screen space with ImGui.
    const float sx = (ndc_x * 0.5f + 0.5f) * ui_w;
    const float sy = (ndc_y * 0.5f + 0.5f) * ui_h;

    if (ndc_x < -1.2f || ndc_x > 1.2f || ndc_y < -1.2f || ndc_y > 1.2f)
        return;

    // Keep label inside center scene band between rails.
    const float rail = ui_chrome::rail_width(ui_w);
    const float min_x = rail + 8.f;
    const float max_x = ui_w - rail - 8.f;
    const float min_y = 8.f;
    const float max_y = ui_h - 8.f;
    if (max_x <= min_x)
        return;

    const float a = billboard_alpha_;
    ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.78f * a);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##block_billboard", nullptr, flags))
    {
        // Soft clamp: if window would sit under rails, ImGui still draws; nudge after size known.
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        float nx = std::clamp(pos.x, min_x, std::max(min_x, max_x - size.x));
        float ny = std::clamp(pos.y, min_y, std::max(min_y, max_y - size.y));
        if (nx != pos.x || ny != pos.y)
            ImGui::SetWindowPos(ImVec2(nx, ny));

        if (billboard_is_uncle_)
            ImGui::TextColored(ImVec4(0.78f, 0.45f, 1.f, 1.f), "uncle");

        if (billboard_height_ >= 0)
            ImGui::Text("H  %d", billboard_height_);
        else
            ImGui::TextDisabled("H  --");

        if (billboard_chain_from_ >= 0 && billboard_chain_to_ >= 0)
            ImGui::Text("%d -> %d", billboard_chain_from_, billboard_chain_to_);
        else
            ImGui::TextDisabled("-- -> --");

        if (billboard_txn_count_ >= 0)
            ImGui::Text("%d txn%s", billboard_txn_count_,
                        billboard_txn_count_ == 1 ? "" : "s");
        else
            ImGui::TextDisabled("-- txns");
        if (billboard_alph_[0] != '\0')
            ImGui::Text("%s ALPH", billboard_alph_);
        else
            ImGui::TextDisabled("-- ALPH");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void BlockflowOverlay::draw_timeline_minimap_(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float rail_w = ui_chrome::rail_width(ui_w);
    const float bar_h = 84.f;
    const float pad = 10.f;
    const float bar_w = std::max(120.f, ui_w - 2.f * rail_w - 2.f * pad);
    const float bar_x = rail_w + pad;
    const float bar_y = ui_h - bar_h - pad;

    ImGui::SetNextWindowPos(ImVec2(bar_x, bar_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bar_w, bar_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##timeline_minimap", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    // Sticky origin for Z math (must match presenter + barrier planes).
    const int64_t origin = ui.timeline_origin_ms > 0
                               ? ui.timeline_origin_ms
                               : static_cast<int64_t>(std::time(nullptr)) * 1000 -
                                     static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    const float mps = ui.meters_per_second > 1e-6f ? ui.meters_per_second : 1.f;

    // High-level overview: genesis-aligned G_seg bins, Z-proportional (match cubes/planes).
    // Click a bin → teleport to that segment's start (from_ms / older edge).
    struct Slot
    {
        int     lookback_k = -1;
        int     segment_id = -1; // G_seg
        float   z_new = 0.f;
        float   z_old = 0.f;
        float   z_start = 0.f; // teleport target (segment start)
        float   load = 0.f;
        int     block_count = 0;
        int     expected = 0;
        bool    full = false;
        bool    known = false; // HUD / snapshot has load data
        bool    valid = false;
        bool    is_live = false;
    };

    constexpr int kMaxOverviewBins = 24;
    const float seg_step_z = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS) * mps;
    const float live_tip_z = camera_.live_scroll_z();
    const float caret_z = camera_.scroll_z();
    const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    const int64_t window_ms =
        static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    const int64_t genesis_ms =
        ui.genesis_ms > 0 ? ui.genesis_ms : ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    const int G_live =
        window_ms > 0 && now_ms > genesis_ms
            ? static_cast<int>((now_ms - genesis_ms) / window_ms)
            : 0;
    auto ts_to_z = [&](int64_t ts_ms) -> float {
        return -static_cast<float>(ts_ms - origin) * 0.001f * mps;
    };

    // HUD by lookback k and by global segment id.
    float hud_load_k[64]{};
    int hud_blocks_k[64]{};
    int hud_exp_k[64]{};
    int hud_full_k[64]{};
    for (int i = 0; i < 64; ++i)
        hud_load_k[i] = -1.f;
    std::unordered_map<int, int> gseg_to_ui;
    for (int i = 0; i < ui.segment_count && i < UiSnapshot::kMaxTimeSegments; ++i)
    {
        const auto& s = ui.segments[i];
        if (s.index >= 0 && s.index < 64)
        {
            hud_load_k[s.index] = s.load_ratio;
            hud_blocks_k[s.index] = s.block_count;
            hud_exp_k[s.index] = s.expected_blocks;
            hud_full_k[s.index] = s.confirmed_full;
        }
        if (s.global_index >= 0)
            gseg_to_ui[s.global_index] = i;
    }

    const int n_want = std::min(kMaxOverviewBins, std::max(1, G_live + 1));
    const int G_first = std::max(0, G_live - (n_want - 1));

    Slot slots[kMaxOverviewBins]{};
    int nslot = 0;
    // Older left → newer right: G_first .. G_live
    for (int G_seg = G_first; G_seg <= G_live && nslot < kMaxOverviewBins; ++G_seg)
    {
        Slot sl;
        sl.segment_id = G_seg;
        sl.lookback_k = G_live - G_seg;
        sl.is_live = (G_seg == G_live);
        int64_t from_ms = genesis_ms + static_cast<int64_t>(G_seg) * window_ms;
        int64_t to_ms = from_ms + window_ms;
        if (sl.is_live && now_ms < to_ms)
            to_ms = std::max(from_ms + 1, now_ms);
        if (to_ms <= from_ms)
            to_ms = from_ms + 1;
        const float z0 = ts_to_z(from_ms);
        const float z1 = ts_to_z(to_ms);
        sl.z_new = std::min(z0, z1);
        sl.z_old = std::max(z0, z1);
        if (sl.is_live)
            sl.z_new = std::min(sl.z_new, live_tip_z);
        // Segment start = older edge; slight inset so cam_k lands inside window.
        sl.z_start = sl.z_old - std::min(2.f, 0.05f * std::max(1.f, sl.z_old - sl.z_new));

        const int k = sl.lookback_k;
        if (k >= 0 && k < 64 && hud_load_k[k] >= 0.f)
        {
            sl.load = hud_load_k[k];
            sl.block_count = hud_blocks_k[k];
            sl.expected = hud_exp_k[k];
            sl.full = hud_full_k[k] != 0;
            sl.known = true;
        }
        else
        {
            auto it = gseg_to_ui.find(G_seg);
            if (it != gseg_to_ui.end())
            {
                const auto& s = ui.segments[it->second];
                sl.load = s.load_ratio;
                sl.block_count = s.block_count;
                sl.expected = s.expected_blocks;
                sl.full = s.confirmed_full != 0;
                sl.known = true;
            }
            else
            {
                sl.load = 0.f;
                sl.block_count = 0;
                sl.expected = 0;
                sl.full = false;
                sl.known = false;
            }
        }
        sl.valid = true;
        slots[nslot++] = sl;
    }

    float z_lo = live_tip_z;
    float z_hi = live_tip_z + seg_step_z;
    if (nslot > 0)
    {
        z_lo = slots[0].z_new;
        z_hi = slots[0].z_old;
        for (int i = 1; i < nslot; ++i)
        {
            z_lo = std::min(z_lo, slots[i].z_new);
            z_hi = std::max(z_hi, slots[i].z_old);
        }
    }
    {
        const float zpad = std::max(1.f, (z_hi - z_lo) * 0.01f);
        z_lo -= zpad;
        z_hi += zpad;
    }
    const float z_span = std::max(1.f, z_hi - z_lo);

    // Older (high Z) → left (t=0); newer (low Z) → right (t=1). Same for bars + caret.
    auto z_to_t = [&](float z) -> float {
        return (z_hi - z) / z_span;
    };

    auto page_older_one = [&]() {
        camera_.set_scroll_z_immediate(camera_.scroll_z() + seg_step_z);
    };
    auto page_newer_one = [&]() {
        if (nslot > 0 && slots[nslot - 1].is_live)
        {
            const Slot& L = slots[nslot - 1];
            const float z = camera_.scroll_z();
            const float span = std::max(1.f, L.z_old - L.z_new);
            if (z >= L.z_new - 0.5f && z <= L.z_old + 0.5f &&
                z <= L.z_new + 0.25f * span)
            {
                camera_.reattach_timeline();
                return;
            }
        }
        camera_.set_scroll_z_immediate(camera_.scroll_z() - seg_step_z);
    };

    auto teleport_to_slot = [&](const Slot& sl) {
        if (!sl.valid)
            return;
        if (sl.is_live)
            camera_.reattach_timeline();
        else
            camera_.set_scroll_z_immediate(sl.z_start);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetCursorScreenPos();
    const float label_band = 14.f;
    const float track_h = 40.f;
    const float track_y = wp.y + 22.f + label_band;
    const float track_w = ImGui::GetContentRegionAvail().x;
    constexpr float kRounding = 6.f;

    // Inset track well.
    dl->AddRectFilled(ImVec2(wp.x, track_y), ImVec2(wp.x + track_w, track_y + track_h),
                      IM_COL32(12, 14, 18, 240), kRounding);
    dl->AddRect(ImVec2(wp.x, track_y), ImVec2(wp.x + track_w, track_y + track_h),
                IM_COL32(48, 54, 66, 220), kRounding, 0, 1.f);
    dl->AddLine(ImVec2(wp.x + 4.f, track_y + 1.f), ImVec2(wp.x + track_w - 4.f, track_y + 1.f),
                IM_COL32(255, 255, 255, 18), 1.f);

    // Precompute screen X for each slot (Z-proportional).
    float slot_x0[kMaxOverviewBins]{};
    float slot_x1[kMaxOverviewBins]{};
    for (int i = 0; i < nslot; ++i)
    {
        // z_old (older) → left; z_new (newer) → right.
        float t0 = std::clamp(z_to_t(slots[i].z_old), 0.f, 1.f);
        float t1 = std::clamp(z_to_t(slots[i].z_new), 0.f, 1.f);
        if (t1 < t0)
            std::swap(t0, t1);
        slot_x0[i] = wp.x + t0 * track_w;
        slot_x1[i] = wp.x + t1 * track_w;
        if (slot_x1[i] - slot_x0[i] < 2.f)
            slot_x1[i] = slot_x0[i] + 2.f;
    }

    // Hover bin from mouse X (before draw so we can highlight).
    int hover_bin = -1;
    {
        const float mx = ImGui::GetIO().MousePos.x;
        const float my = ImGui::GetIO().MousePos.y;
        if (my >= track_y - 2.f && my <= track_y + track_h + 8.f && mx >= wp.x &&
            mx <= wp.x + track_w)
        {
            for (int i = 0; i < nslot; ++i)
            {
                if (mx >= slot_x0[i] && mx <= slot_x1[i])
                {
                    hover_bin = i;
                    break;
                }
            }
            // Edge gaps: nearest by center.
            if (hover_bin < 0 && nslot > 0)
            {
                float best = 1e9f;
                for (int i = 0; i < nslot; ++i)
                {
                    const float mid = 0.5f * (slot_x0[i] + slot_x1[i]);
                    const float d = std::abs(mx - mid);
                    if (d < best)
                    {
                        best = d;
                        hover_bin = i;
                    }
                }
            }
        }
    }

    // Camera-bin for subsegment ticks (64s grid; keep in sync with ALPH_SUBSEGMENT_SECONDS).
    int cam_bin = -1;
    for (int i = 0; i < nslot; ++i)
    {
        if (caret_z >= slots[i].z_new - 0.25f && caret_z <= slots[i].z_old + 0.25f)
        {
            cam_bin = i;
            break;
        }
    }

    for (int i = 0; i < nslot; ++i)
    {
        if (!slots[i].valid)
            continue;
        const float x0 = slot_x0[i];
        const float x1 = slot_x1[i];
        const float cell_w = x1 - x0;

        // Segment separators (subtle).
        if (i > 0)
            dl->AddLine(ImVec2(x0, track_y + 3.f), ImVec2(x0, track_y + track_h - 3.f),
                        IM_COL32(70, 78, 96, 90), 1.f);

        // Hover highlight behind fill.
        if (i == hover_bin)
            dl->AddRectFilled(ImVec2(x0, track_y + 1.f),
                              ImVec2(x1, track_y + track_h - 1.f),
                              IM_COL32(255, 255, 255, 22), 2.f);

        if (slots[i].known)
        {
            float hist = slots[i].load;
            if (slots[i].expected > 0 && slots[i].block_count > 0)
            {
                const float dens = static_cast<float>(slots[i].block_count) /
                                   static_cast<float>(slots[i].expected);
                hist = std::max(hist, dens);
            }
            hist = std::clamp(hist, 0.f, 1.f);
            if (hist > 0.02f)
            {
                const float h = track_h * std::max(0.08f, hist);
                const float y0 = track_y + track_h - h;
                // Monochrome load: slate → teal; live = tip green.
                const int a = slots[i].is_live
                                  ? static_cast<int>(140 + 100.f * hist)
                                  : static_cast<int>(100 + 120.f * hist);
                ImU32 col = slots[i].is_live
                                ? IM_COL32(50, 200, 120, a)
                                : IM_COL32(70, 130, 150, a);
                dl->AddRectFilled(ImVec2(x0 + 0.5f, y0), ImVec2(x1 - 0.5f, track_y + track_h - 1.f),
                                  col, 2.f);
                // Soft top edge on fill.
                dl->AddLine(ImVec2(x0 + 1.f, y0), ImVec2(x1 - 1.f, y0),
                            IM_COL32(200, 230, 240, 40), 1.f);
            }
        }

        if (slots[i].full)
            dl->AddRect(ImVec2(x0 + 0.5f, track_y + 2.f),
                        ImVec2(x1 - 0.5f, track_y + track_h - 2.f),
                        IM_COL32(220, 230, 255, 110), 2.f, 0, 1.f);

        // Subsegment ticks on camera or hovered cell only (ALPH_SUBSEGMENT_SECONDS).
        if ((i == cam_bin || i == hover_bin) && cell_w > 28.f)
        {
            const float span_z = std::max(1.f, slots[i].z_old - slots[i].z_new);
            const float sub_z = static_cast<float>(ALPH_SUBSEGMENT_SECONDS) * mps;
            if (sub_z > 0.f && span_z > sub_z * 1.5f)
            {
                for (float z = slots[i].z_new + sub_z; z < slots[i].z_old - 0.5f * sub_z;
                     z += sub_z)
                {
                    const float tx = wp.x + std::clamp(z_to_t(z), 0.f, 1.f) * track_w;
                    if (tx > x0 + 2.f && tx < x1 - 2.f)
                        dl->AddLine(ImVec2(tx, track_y + track_h - 8.f),
                                    ImVec2(tx, track_y + track_h - 2.f),
                                    IM_COL32(180, 200, 220, 70), 1.f);
                }
            }
        }

        // Labels above track (not over fill).
        char lab[28];
        if (slots[i].is_live)
            std::snprintf(lab, sizeof(lab), "Live #%d", slots[i].segment_id);
        else
            std::snprintf(lab, sizeof(lab), "#%d", slots[i].segment_id);
        const ImVec2 ts = ImGui::CalcTextSize(lab);
        const float label_y = track_y - label_band + 1.f;
        if (ts.x < cell_w - 4.f)
        {
            dl->AddText(ImVec2(0.5f * (x0 + x1) - 0.5f * ts.x, label_y),
                        slots[i].is_live ? IM_COL32(120, 230, 170, 240)
                                         : IM_COL32(200, 210, 220, 210),
                        lab);
        }
        else if (cell_w > 12.f &&
                 (i == 0 || i == nslot - 1 || slots[i].is_live || (i % 4) == 0))
        {
            std::snprintf(lab, sizeof(lab), "%d", slots[i].segment_id);
            const ImVec2 ts2 = ImGui::CalcTextSize(lab);
            if (ts2.x < cell_w - 2.f)
                dl->AddText(ImVec2(0.5f * (x0 + x1) - 0.5f * ts2.x, label_y),
                            IM_COL32(180, 190, 200, 180), lab);
        }
    }

    // Caret at camera Z (same Z→X map as cells).
    const float cam_t = std::clamp(z_to_t(caret_z), 0.f, 1.f);
    const float cx = wp.x + cam_t * track_w;
    // Soft glow line
    dl->AddLine(ImVec2(cx, track_y - 1.f), ImVec2(cx, track_y + track_h + 1.f),
                IM_COL32(255, 210, 80, 70), 5.f);
    dl->AddLine(ImVec2(cx, track_y - 2.f), ImVec2(cx, track_y + track_h + 2.f),
                IM_COL32(255, 220, 90, 240), 1.5f);
    // Diamond head above track
    const float dy = track_y - 2.f;
    dl->AddTriangleFilled(ImVec2(cx, dy), ImVec2(cx - 5.5f, dy - 8.f),
                          ImVec2(cx + 5.5f, dy - 8.f), IM_COL32(255, 220, 90, 255));
    // Small pill at mid-track
    dl->AddRectFilled(ImVec2(cx - 3.f, track_y + track_h * 0.5f - 5.f),
                      ImVec2(cx + 3.f, track_y + track_h * 0.5f + 5.f),
                      IM_COL32(255, 220, 90, 220), 3.f);

    ImGui::SetCursorScreenPos(ImVec2(wp.x, track_y - 2.f));
    ImGui::InvisibleButton("##minimap_track", ImVec2(track_w, track_h + 8.f));
    const bool track_hot = ImGui::IsItemHovered() || ImGui::IsItemActive();

    if (track_hot && nslot > 0)
    {
        const float mx = ImGui::GetIO().MousePos.x;
        const float t = (mx - wp.x) / std::max(1.f, track_w);
        int bin = hover_bin;
        if (bin < 0)
        {
            // Fallback: nearest center.
            float best = 1e9f;
            for (int i = 0; i < nslot; ++i)
            {
                const float mid = 0.5f * (slot_x0[i] + slot_x1[i]);
                const float d = std::abs(mx - mid);
                if (d < best)
                {
                    best = d;
                    bin = i;
                }
            }
        }
        bin = std::clamp(bin, 0, nslot - 1);

        // Click (not drag): teleport to segment start.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            minimap_dragging_ = false;
            teleport_to_slot(slots[bin]);
        }
        // Drag past edges still pages one segment (rate-limited).
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.f))
        {
            minimap_dragging_ = true;
            const double now_sec = ImGui::GetTime();
            constexpr double kEdgePageCooldown = 0.14;
            if (t < -0.02f && now_sec >= minimap_edge_page_next_sec_)
            {
                page_older_one();
                minimap_edge_page_next_sec_ = now_sec + kEdgePageCooldown;
            }
            else if (t > 1.02f && now_sec >= minimap_edge_page_next_sec_)
            {
                page_newer_one();
                minimap_edge_page_next_sec_ = now_sec + kEdgePageCooldown;
            }
        }

        if (ImGui::IsItemHovered() && slots[bin].valid)
        {
            if (slots[bin].known)
                ImGui::SetTooltip(
                    "Segment #%d  (lookback k=%d)  load=%.0f%%  blocks=%d\n"
                    "Click: jump to segment start | Live bin / key 3: tip",
                    slots[bin].segment_id, slots[bin].lookback_k, slots[bin].load * 100.f,
                    slots[bin].block_count);
            else
                ImGui::SetTooltip(
                    "Segment #%d  (lookback k=%d)  load unknown\n"
                    "Click: jump to segment start | Live bin / key 3: tip",
                    slots[bin].segment_id, slots[bin].lookback_k);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        minimap_dragging_ = false;

    ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
    if (ImGui::SmallButton("Live"))
        camera_.reattach_timeline();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Return to live tip (key 3)");
    ImGui::SameLine();
    if (ImGui::SmallButton("< Seg"))
        page_older_one();
    ImGui::SameLine();
    if (ImGui::SmallButton("Seg >"))
        page_newer_one();
    ImGui::SameLine();
    {
        const bool end_on =
            camera_.view_preset() == CameraController::ViewPreset::End;
        if (end_on)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton("End"))
            camera_.set_view_preset(CameraController::ViewPreset::End);
        if (end_on)
            ImGui::EndDisabled();
        ImGui::SameLine();
        const bool side_on =
            camera_.view_preset() == CameraController::ViewPreset::Side;
        if (side_on)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton("Side"))
            camera_.set_view_preset(CameraController::ViewPreset::Side);
        if (side_on)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("View"))
            camera_.toggle_view_preset();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "End (1): ring +Z | Side (2): timeline profile\n"
                "V: toggle | 3: Live tip | Side: L/R = Z, U/D = orbit");
    }
    ImGui::SameLine();
    if (nslot > 0)
    {
        ImGui::TextDisabled("#%d–#%d · click to jump", slots[0].segment_id,
                            slots[nslot - 1].segment_id);
    }
    else
        ImGui::TextDisabled("timeline overview");

    ImGui::End();
}

void BlockflowOverlay::draw_network(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float rail_w = ui_chrome::rail_width(ui_w);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, ui_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Network", nullptr, flags);

    // Domain combo
    ImGui::TextDisabled("Domain");
    const char* labels[] = { "Mainnet", "Testnet", "Debug" };
    int domain_i = static_cast<int>(domain_);
    if (domain_i < 0 || domain_i > 2)
        domain_i = 0;
    if (ImGui::BeginCombo("##domain", labels[domain_i]))
    {
        for (int i = 0; i < 3; ++i) // Mainnet + Testnet + Debug (FakeChain)
        {
            const bool selected = (domain_i == i);
            if (ImGui::Selectable(labels[i], selected))
            {
                const NetworkDomain next = static_cast<NetworkDomain>(i);
                if (next != domain_ && !engine_.network_is_switching())
                {
                    const std::string url = resolve_url_(next);
                    if (engine_.switch_network_domain(i, url))
                    {
                        domain_ = next;
                        session_start_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
                        camera_.reattach_timeline();
                        save_prefs();
                    }
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            if (i == static_cast<int>(NetworkDomain::Debug) && ImGui::IsItemHovered())
                ImGui::SetTooltip("Offline FakeChain simulator (no HTTP)");
        }
        ImGui::EndCombo();
    }

    const std::string url_fallback = resolve_url_(domain_);
    const char* url_show = ui.net_base_url[0] ? ui.net_base_url : url_fallback.c_str();
    ImGui::TextDisabled("Endpoint");
    ImGui::TextWrapped("%s", url_show);
    if (ImGui::IsItemHovered() && url_show[0])
        ImGui::SetTooltip("%s", url_show);

    // Status pill (adapter sets History / Catching up when off live or catching up).
    const NetworkStatus st = static_cast<NetworkStatus>(
        ui.net_switching ? static_cast<int>(NetworkStatus::Switching) : ui.net_status);
    ImVec4 status_col(0.7f, 0.7f, 0.7f, 1.f);
    if (st == NetworkStatus::Steady)
        status_col = ImVec4(0.25f, 0.9f, 0.4f, 1.f);
    else if (st == NetworkStatus::History || st == NetworkStatus::CatchingUp)
        status_col = ImVec4(0.45f, 0.85f, 1.f, 1.f);
    else if (st == NetworkStatus::Bootstrapping || st == NetworkStatus::IdentifyTips ||
             st == NetworkStatus::ConfirmWalk || st == NetworkStatus::Connecting)
        status_col = ImVec4(1.f, 0.75f, 0.2f, 1.f);
    else if (st == NetworkStatus::Switching)
        status_col = ImVec4(0.4f, 0.8f, 1.f, 1.f);
    else if (st == NetworkStatus::Error)
        status_col = ImVec4(1.f, 0.3f, 0.25f, 1.f);
    ImGui::Text("Status");
    ImGui::SameLine();
    ImGui::TextColored(status_col, "%s", network_status_label(st));
    // Secondary line: gaps / deps / history (primary stays Stable when live is usable).
    if (ui.net_status_detail[0] != '\0')
        ImGui::TextDisabled("%s", ui.net_status_detail);
    else if (st == NetworkStatus::History)
        ImGui::TextDisabled("Live tip halted until camera returns to tip window");
    else if (st == NetworkStatus::CatchingUp)
        ImGui::TextDisabled("Filling missed sub-segments after History");
    else if (st == NetworkStatus::Steady)
        ImGui::TextDisabled("Live tip");

    ImGui::Separator();
    ImGui::Text("Loading");

    const float win_need = static_cast<float>(std::max(1, ui.lookback_windows_need));
    const float win_frac =
        std::clamp(static_cast<float>(ui.lookback_windows_done) / win_need, 0.f, 1.f);
    ImGui::ProgressBar(win_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("windows %d / %d", ui.lookback_windows_done, ui.lookback_windows_need);

    const float frontier_frac =
        std::clamp(static_cast<float>(ui.lanes_with_frontier) / 16.f, 0.f, 1.f);
    ImGui::ProgressBar(frontier_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("frontier lanes %d / 16", ui.lanes_with_frontier);
    ImGui::TextDisabled("open confirm walks %d", ui.open_confirm_walks);

    float pool_frac = 0.f;
    if (ui.total_blocks > 0)
        pool_frac = std::clamp(static_cast<float>(ui.total_blocks) / 256.f, 0.f, 1.f);
    ImGui::ProgressBar(pool_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("pool blocks %d", ui.total_blocks);
    ImGui::TextDisabled("fetches admitted %d", ui.stats_fetch_admitted);
    ImGui::Separator();
    ImGui::Text("Disk cache");
    if (ui.disk_cache_path[0] != '\0')
        ImGui::TextWrapped("%s", ui.disk_cache_path);
    else
        ImGui::TextDisabled("path: (domain not ready)");
    ImGui::TextDisabled("on disk: %d G-seg · %d MB", ui.disk_cache_segments, ui.disk_cache_mb);
    if (ui.disk_cache_boot_blocks > 0)
        ImGui::TextDisabled("boot: %d blocks from cache", ui.disk_cache_boot_blocks);
    if (ui.disk_cache_last_event[0] != '\0')
        ImGui::TextWrapped("last: %s", ui.disk_cache_last_event);
    else
        ImGui::TextDisabled("last: (waiting for Stable / stop flush)");
    if (ui.cache_pressure_level >= 2)
        ImGui::TextColored(ImVec4(1.f, 0.45f, 0.2f, 1.f),
                           "Timeline RAM HARD — oldest blocks may drop");
    else if (ui.cache_pressure_level >= 1)
        ImGui::TextColored(ImVec4(1.f, 0.85f, 0.3f, 1.f),
                           "Timeline RAM large — history kept until hard cap");

    // Sliding ring summary — full per-window detail on hover (tooltip).
    if (ui.segment_count > 0)
    {
        const int nshow =
            std::min(ui.segment_count, UiSnapshot::kMaxTimeSegments);
        ImGui::TextDisabled("Segments (%d) · hover for detail", nshow);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Lookback windows (older -> newer in ring)");
            ImGui::Separator();
            for (int i = 0; i < nshow; ++i)
            {
                const auto& s = ui.segments[i];
                const int64_t span_ms = std::max<int64_t>(1, s.to_ms - s.from_ms);
                const float span_s = static_cast<float>(span_ms) * 0.001f;
                const float ratio = std::clamp(s.load_ratio, 0.f, 1.f);
                const bool is_live = (s.index == 0);
                const char* tag =
                    is_live ? "Live" : (s.confirmed_full ? "full" : "loading");
                if (is_live)
                    ImGui::Text("k%d Live  %.0fs  %d%%  %d blks  %s", s.index, span_s,
                                static_cast<int>(ratio * 100.f + 0.5f), s.block_count, tag);
                else
                    ImGui::Text("k%d       %.0fs  %d%%  %d blks  %s", s.index, span_s,
                                static_cast<int>(ratio * 100.f + 0.5f), s.block_count, tag);
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::Separator();
    ImGui::Text("Activity");
    ImGui::TextDisabled("phase: %s", network_status_label(st));
    ImGui::Text("confirmed frontier tips: %d", ui.confirmed_tip_count);
    ImGui::Text("live tips: %d", ui.tip_count);
    ImGui::TextDisabled("is_main API calls: %d", ui.stats_api_is_main);
    ImGui::TextDisabled("removed: %d", ui.stats_removed);
    ImGui::TextDisabled("seed queue: %d", ui.stats_seed_q);

    if (ui.last_poll_ms > 0)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const float age_s = static_cast<float>(now - ui.last_poll_ms) * 0.001f;
        ImGui::TextDisabled("last poll: %.1fs ago", age_s);
        ImGui::TextDisabled("poll interval: %.0fs", ui.poll_interval_sec);
    }
    else
        ImGui::TextDisabled("last poll: â€”");

    // One shard per line so nothing clips in the narrow rail.
    if (ImGui::TreeNode("Shard tips (H_c / net tip)"))
    {
        for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        {
            for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            {
                const int lane = f * ALPH_NUM_GROUPS + t;
                const int hc = ui.confirmed_height_by_lane[lane];
                const int tip = ui.tip_height_by_lane[lane];
                if (hc < 0 && tip < 0)
                    ImGui::TextDisabled("  %dâ†’%d:  â€” / â€”", f, t);
                else if (hc < 0)
                    ImGui::Text("  %dâ†’%d:  â€” / %d", f, t, tip);
                else if (tip < 0)
                    ImGui::Text("  %dâ†’%d:  %d / â€”", f, t, hc);
                else
                    ImGui::Text("  %dâ†’%d:  %d / %d", f, t, hc, tip);
            }
        }
        ImGui::TreePop();
    }

    // Former bottom "Blockflow" toolbar — collapsible under Network.
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Blockflow", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Checkbox("Multi-tx only", &filter_multi_tx_))
        {
            engine_.set_scene_filter_multi_tx(filter_multi_tx_);
            save_prefs();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show only blocks with more than 1 transaction\n(hides coinbase-only / unknown).");
        {
            float min_alph_f = static_cast<float>(filter_min_alph_);
            if (ImGui::DragFloat("Min ALPH out", &min_alph_f, 0.1f, 0.f, 1.0e9f, "%.3f",
                                 ImGuiSliderFlags_AlwaysClamp))
            {
                set_filter_min_alph(static_cast<double>(min_alph_f));
                save_prefs();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Hide blocks whose total output ALPH is below this value.\n"
                    "0 = off. Unknown amounts are hidden when active.");
        }
        if (ImGui::Button("Screenshot (F12)"))
            engine_.request_screenshot(nullptr);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Save client PNG under docs/images/capture_*.png");
        ImGui::TextDisabled("F11 fullscreen  ·  Esc exit FS / quit");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "F11: borderless fullscreen (monitor of the window)\n"
                "Esc in fullscreen: return to windowed\n"
                "Esc windowed: quit application");

        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const float elapsed_ms = static_cast<float>(now - session_start_ms_) + 1e-3f;
        const float bps = ui.total_blocks / (0.001f * elapsed_ms);

        ImGui::Text("camera z: %.1f", camera_.camera().eye.z);
        ImGui::TextDisabled("  (Up/Down / wheel)");
        ImGui::Text("blocks: %d", ui.total_blocks);
        ImGui::Text("rate: %1.2f/s", bps);
        ImGui::Text("frontier tips: %d", ui.confirmed_tip_count);
        ImGui::Text("live tips: %d", ui.tip_count);

        ImGui::Spacing();
        ImGui::TextDisabled("Confirmed H_c by shard:");
        for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        {
            for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            {
                const int lane = f * ALPH_NUM_GROUPS + t;
                const int hc = ui.confirmed_height_by_lane[lane];
                if (hc < 0)
                    ImGui::TextDisabled("  %dâ†’%d:  â€”", f, t);
                else
                    ImGui::Text("  %dâ†’%d:  %d", f, t, hc);
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Recent feed");
        ImGui::BeginChild("feed", ImVec2(0, 180.f), true, ImGuiWindowFlags_HorizontalScrollbar);
        int feed_i = 0;
        for (const FeedEntry& entry : ui.feed)
        {
            ImGui::PushID(feed_i++);
            ImGui::PushID(entry.hash.c_str());
            const int shardId = entry.chain_idx() & 15;
            ImGui::TextColored(
                ImVec4(kShardColors[shardId].r, kShardColors[shardId].g,
                       kShardColors[shardId].b, 1.0f),
                "[%dâ†’%d]", entry.chainFrom, entry.chainTo);
            ImGui::SameLine();
            // Shorten hash label in narrow rail; full hash on hover / clipboard.
            char short_h[20];
            if (entry.hash.size() > 12)
                std::snprintf(short_h, sizeof(short_h), "%.6sâ€¦%.4s", entry.hash.c_str(),
                              entry.hash.c_str() + entry.hash.size() - 4);
            else
                std::snprintf(short_h, sizeof(short_h), "%s", entry.hash.c_str());
            if (ImGui::SmallButton(short_h))
            {
                engine_.set_selection(entry.hash);
                ImGui::SetClipboardText(entry.hash.c_str());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n(click select + copy)", entry.hash.c_str());
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

namespace
{
// Compact hash for tree labels (full id still available via link when expanded).
void short_id(const std::string& s, char* buf, size_t buf_n)
{
    if (s.empty())
    {
        snprintf(buf, buf_n, "â€”");
        return;
    }
    if (s.size() <= 14)
    {
        snprintf(buf, buf_n, "%s", s.c_str());
        return;
    }
    snprintf(buf, buf_n, "%.6sâ€¦%.4s", s.c_str(), s.c_str() + s.size() - 4);
}

// Host set by draw_inspector from current domain (mainnet/testnet explorer).
const char* g_explorer_host = "https://explorer.alephium.org";

void explorer_url(char* buf, size_t n, const char* kind, const std::string& id)
{
    snprintf(buf, n, "%s/%s/%s", g_explorer_host, kind, id.c_str());
}

// One UTXO row: link + amount (collapsed lists stay one line).
void draw_utxo_row(UTXO& u, const char* kind, int index)
{
    ImGui::PushID(index);
    char url[512];
    char label[96];
    short_id(u.address, label, sizeof(label));
    explorer_url(url, sizeof(url), "addresses", u.address);

    ImGui::Bullet();
    ImGui::TextDisabled("%s", kind);
    ImGui::SameLine();
    ImGui::TextLinkOpenURL(u.address.empty() ? "##addr" : label, url);
    if (ImGui::IsItemHovered() && !u.address.empty())
        ImGui::SetTooltip("%s", u.address.c_str());
    ImGui::SameLine();
    ImGui::Text("%s ALPH", u.toAmount().c_str());
    if (!u.key.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("key: %s\nhint: %d", u.key.c_str(), u.hint);
    ImGui::PopID();
}
} // namespace

void BlockflowOverlay::draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float rail_w = ui_chrome::rail_width(ui_w);
    g_explorer_host = network_domain_explorer_host(domain_);
    // Cleared unless a Deps row is hovered this frame (recolors selection arrows).
    std::string dep_hover_this_frame;

    // Prefer live selection detail if feed click updated selection this frame
    AlphBlock inspector = ui.selected_detail;
    {
        AlphBlock live = engine_.copy_selected_block();
        if (!live.hash.empty() &&
            (inspector.hash != live.hash || inspector.txns.empty()))
            inspector = std::move(live);
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::SetNextWindowPos(ImVec2(ui_w - rail_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, ui_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Block", nullptr, flags);

    char url[512];
    char id_buf[64];
    if (!inspector.hash.empty())
    {
        explorer_url(url, sizeof(url), "blocks", inspector.hash);
        short_id(inspector.hash, id_buf, sizeof(id_buf));

        // â”€â”€ Block header (always open, compact) â”€â”€
        ImGui::TextDisabled("hash");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL(id_buf, url);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n(click opens explorer)", inspector.hash.c_str());

        const int shardId = inspector.chain_idx() & 15;
        ImGui::Text("height %d", inspector.height);
        ImGui::SameLine(0.f, 16.f);
        ImGui::TextColored(
            ImVec4(kShardColors[shardId].r, kShardColors[shardId].g, kShardColors[shardId].b, 1.0f),
            "chain [%dâ†’%d]", inspector.chainFrom, inspector.chainTo);

        if (inspector.timestamp > 0)
        {
            const time_t t = static_cast<time_t>(inspector.timestamp / 1000);
            char tbuf[40];
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            ImGui::TextDisabled("time %s", tbuf);
        }

        {
            const int tx_n = (inspector.txn_count >= 0)
                                 ? inspector.txn_count
                                 : static_cast<int>(inspector.txns.size());
            ImGui::TextDisabled("%d tx · %d deps · %d uncles",
                                tx_n,
                                static_cast<int>(inspector.deps.size()),
                                static_cast<int>(inspector.uncles.size()));
        }
        if (scene_ && ImGui::SmallButton("Replay deps"))
            scene_->request_walk_replay();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-run multi-hop dep trace (R)");

        // --- Dependencies: hover recolors 3D arrow; click selects + looks (no explorer) ---
        if (!inspector.deps.empty() &&
            ImGui::TreeNodeEx("##deps", ImGuiTreeNodeFlags_SpanAvailWidth,
                              "Deps (%d)", static_cast<int>(inspector.deps.size())))
        {
            int di = 0;
            for (const std::string& dep : inspector.deps)
            {
                ImGui::PushID(di++);
                short_id(dep, id_buf, sizeof(id_buf));
                ImGui::Bullet();
                if (!dep.empty() &&
                    ImGui::Selectable(id_buf, false, ImGuiSelectableFlags_DontClosePopups))
                    engine_.set_selection(dep);
                if (ImGui::IsItemHovered() && !dep.empty())
                {
                    dep_hover_this_frame = dep;
                    ImGui::SetTooltip("%s\n(click to select / look at)", dep.c_str());
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        // â”€â”€ Transactions: one-line headers, expand for detail â”€â”€
        {
            const std::string block_alph = alph_sum_block_outputs(inspector);
            if (!block_alph.empty() && block_alph != "0")
                ImGui::Text("Block out: %s ALPH", alph_atto_to_display(block_alph).c_str());
            else if (!inspector.alph_out_atto.empty() && inspector.alph_out_atto != "0")
                ImGui::Text("Block out: %s ALPH",
                            alph_atto_to_display(inspector.alph_out_atto).c_str());
        }
        ImGui::Text("Transactions (%d)", static_cast<int>(inspector.txns.size()));
        if (inspector.txns.empty())
            ImGui::TextDisabled("No transactions loaded (detail may still be fetching).");

        // Lightweight: only clip closed rows when many txs (open rows still expand below).
        const int txn_count = static_cast<int>(inspector.txns.size());
        for (int tx_i = 0; tx_i < txn_count; ++tx_i)
        {
            AlphTxn& tx = inspector.txns[static_cast<size_t>(tx_i)];
            ImGui::PushID(tx_i);
            ImGui::PushID(tx.txid.c_str());

            short_id(tx.txid, id_buf, sizeof(id_buf));
            const int n_in = static_cast<int>(tx.inputs.size());
            const int n_out = static_cast<int>(tx.outputs.size());
            const std::string tx_alph = alph_sum_txn_outputs(tx);
            const std::string tx_alph_disp =
                (!tx_alph.empty() && tx_alph != "0") ? alph_atto_to_display(tx_alph) : std::string{};

            char tx_label[192];
            if (tx_alph_disp.empty())
                std::snprintf(tx_label, sizeof(tx_label), "%s  in %d · out %d · gas %d", id_buf,
                              n_in, n_out, tx.gasAmount);
            else
                std::snprintf(tx_label, sizeof(tx_label), "%s  in %d · out %d · gas %d · %s ALPH",
                              id_buf, n_in, n_out, tx.gasAmount, tx_alph_disp.c_str());
            const bool open = ImGui::TreeNodeEx(
                "##tx", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap,
                "%s", tx_label);

            if (ImGui::IsItemHovered() && !tx.txid.empty())
                ImGui::SetTooltip("%s", tx.txid.c_str());

            if (open)
            {
                explorer_url(url, sizeof(url), "transactions", tx.txid);
                ImGui::TextDisabled("txid");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(tx.txid.empty() ? "##txid" : id_buf, url);
                if (ImGui::IsItemHovered() && !tx.txid.empty())
                    ImGui::SetTooltip("%s", tx.txid.c_str());

                ImGui::Text("v%d  net %d  gas %d", tx.version, tx.networkId, tx.gasAmount);
                if (!tx.gasPrice.empty())
                {
                    ImGui::SameLine(0.f, 12.f);
                    ImGui::TextDisabled("price %s", tx.gasPrice.c_str());
                }
                if (!tx.scriptOpt.empty())
                {
                    ImGui::TextDisabled("script");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", tx.scriptOpt.c_str());
                }

                // Nested collapsible I/O (closed by default).
                if (n_in > 0 &&
                    ImGui::TreeNodeEx("##inputs", ImGuiTreeNodeFlags_SpanAvailWidth,
                                      "Inputs (%d)", n_in))
                {
                    for (int i = 0; i < n_in; ++i)
                        draw_utxo_row(tx.inputs[static_cast<size_t>(i)], "in", i);
                    ImGui::TreePop();
                }
                if (n_out > 0 &&
                    ImGui::TreeNodeEx("##outputs", ImGuiTreeNodeFlags_SpanAvailWidth,
                                      "Outputs (%d)", n_out))
                {
                    for (int i = 0; i < n_out; ++i)
                        draw_utxo_row(tx.outputs[static_cast<size_t>(i)], "out", i);
                    ImGui::TreePop();
                }
                if (n_in == 0 && n_out == 0)
                    ImGui::TextDisabled("No inputs/outputs in payload.");

                ImGui::TreePop();
            }

            ImGui::PopID();
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextWrapped(
            "Select a block from the feed below or click a cube in the scene.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Camera: wheel/arrows Z · LMB look · short LMB pick · RMB pan · short RMB deselect+recenter (keep Z) · 3 Live");
        ImGui::TextDisabled(
            "Solid=main+deps Â· green=frontier tip + blockDeps Â· cyan=unconfirmed children of frontier Â· orange=missing deps Â· gold=select");
        {
            // Adapter phases: Bootstrap/IdentifyTips/BfsTrace/Steady (UI Stable).
            const char* pname = "Bootstrap";
            if (ui.trace_phase == 1) pname = "Identify tips";
            else if (ui.trace_phase == 2) pname = "BFS confirm";
            else if (ui.trace_phase == 3) pname = "Stable";
            ImGui::TextDisabled(
                "Confirm phase: %s | open BFS workers=%d | rays from [g->g] then gaps",
                pname, ui.trace_offset);
        }
        ImGui::TextDisabled("Tx list: click a row to expand gas, inputs, outputs.");
    }
    engine_.set_ui_dep_hover(dep_hover_this_frame);
    ImGui::End();
}
