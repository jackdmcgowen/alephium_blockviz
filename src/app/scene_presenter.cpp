#include "app/pch.h"
#include "app/scene_presenter.hpp"

#include "domain/alph_block.hpp"
#include "graphics/camera.hpp"
#include "graphics/debug/debug_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <functional>

#include <glm/glm.hpp>

namespace
{
// World units per second of block timestamp (matches camera scroll_z units).
// Keep spacing: do not change meters_per_second or base_radius.
float meters_per_second = 1.0f;
constexpr float kLayoutBaseRadius = 20.0f;

const glm::vec4 kCyanArrowColor(0.15f, 0.95f, 1.0f, 0.95f);   // frontier-child → tip
const glm::vec4 kGreenArrowColor(0.20f, 0.95f, 0.35f, 0.95f); // frontier tip → blockDeps
const glm::vec4 kDeathArrowColor(1.0f, 0.12f, 0.10f, 0.95f);
const glm::vec4 kSelectionArrowColor(1.0f, 0.85f, 0.2f, 1.0f);
const glm::vec4 kHoverArrowColor(1.0f, 0.85f, 0.2f, 0.45f);
const glm::vec4 kBarrierPlaneColor(0.35f, 0.75f, 0.95f, 0.10f);

// Shared helper: layout, planes, and segment cull all use this Z mapping.
inline float ts_to_z(int64_t ts_ms, int64_t origin_ms, float mps)
{
    return -static_cast<float>(ts_ms - origin_ms) * 0.001f * mps;
}

bool inset_segment(const glm::vec3& from, const glm::vec3& to, float clearance,
                   glm::vec3& from_out, glm::vec3& to_out)
{
    const glm::vec3 delta = to - from;
    const float len = glm::length(delta);
    if (len < 2.0f * clearance + 1e-4f)
        return false;
    const glm::vec3 dir = delta / len;
    from_out = from + dir * clearance;
    to_out = to - dir * clearance;
    return true;
}

// Key: listing block (owns deps[]) then dependency. Arrow draws listing â†’ dep.
std::string tip_edge_key(const std::string& listing, const std::string& dep)
{
    return std::string("t|") + listing + '|' + dep;
}

bool parse_tip_edge_key(const std::string& key, std::string& listing, std::string& dep)
{
    if (key.size() < 3 || key[0] != 't' || key[1] != '|')
        return false;
    const size_t p1 = key.find('|', 2);
    if (p1 == std::string::npos)
        return false;
    listing = key.substr(2, p1 - 2);
    dep = key.substr(p1 + 1);
    return !listing.empty() && !dep.empty();
}
} // namespace

ScenePresenter::ScenePresenter(BlockScene& scene)
    : scene_(scene)
{
}

float ScenePresenter::now_sec_() const
{
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - clock0_).count();
}

float ScenePresenter::ephemeral_grow_u_(const std::string& key, float stagger_delay,
                                        std::unordered_set<std::string>& seen)
{
    seen.insert(key);
    const float now = now_sec_();
    auto it = ephemeral_birth_sec_.find(key);
    if (it == ephemeral_birth_sec_.end())
    {
        ephemeral_birth_sec_[key] = now + stagger_delay;
        it = ephemeral_birth_sec_.find(key);
    }
    const float age = now - it->second;
    if (age <= 0.f)
        return 0.f;
    return std::clamp(age / kArrowGrowSec, 0.f, 1.f);
}

void ScenePresenter::tip_dep_tick_and_draw_(
    DebugDrawer& debug,
    const std::unordered_map<std::string, glm::vec3>& positions,
    const std::unordered_set<std::string>& live_nodes,
    const std::unordered_set<std::string>& green_display,
    const std::unordered_set<std::string>& cyan_owners,
    const std::unordered_set<std::string>& frontier_domain,
    float tip_len, float tip_rad, float shaft_r, float clearance)
{
    const float now = now_sec_();
    constexpr uint32_t kDepRadial = 8;
    constexpr uint32_t kMaxDepArrows = 512;

    struct ActiveEdge
    {
        std::string key;
        glm::vec3 from_pos{};
        glm::vec3 to_pos{};
        int stagger_i = 0;
        bool green = false;
    };

    std::unordered_set<std::string> active_keys;
    active_keys.reserve(64);
    std::vector<ActiveEdge> active;
    active.reserve(64);

    auto add_edges_from = [&](const std::string& listing_hash, bool green,
                              bool only_to_frontier_deps, int stagger_base) {
        if (listing_hash.empty() || live_nodes.count(listing_hash) == 0)
            return;
        auto d = scene_.detail_store().get(listing_hash);
        if (!d)
            return;
        auto listing_it = positions.find(listing_hash);
        if (listing_it == positions.end())
            return;

        int stagger_i = 0;
        for (const std::string& dep_hash : d->deps)
        {
            if (dep_hash.empty() || live_nodes.count(dep_hash) == 0)
                continue;
            if (only_to_frontier_deps && frontier_domain.count(dep_hash) == 0)
                continue;
            auto dep_it = positions.find(dep_hash);
            if (dep_it == positions.end())
                continue;
            ActiveEdge e;
            e.key = tip_edge_key(listing_hash, dep_hash);
            e.from_pos = listing_it->second;
            e.to_pos = dep_it->second;
            e.stagger_i = stagger_base + stagger_i++;
            e.green = green;
            active_keys.insert(e.key);
            active.push_back(std::move(e));
        }
    };

    int tip_stagger_base = 0;
    // Green: display frontier tip â†’ all live blockDeps.
    for (const std::string& tip : green_display)
    {
        add_edges_from(tip, /*green=*/true, /*only_to_frontier_deps=*/false, tip_stagger_base);
        tip_stagger_base += 2;
    }
    // Cyan: frontier children â†’ only edges into domain frontier tips.
    for (const std::string& owner : cyan_owners)
    {
        add_edges_from(owner, /*green=*/false, /*only_to_frontier_deps=*/true, tip_stagger_base);
        tip_stagger_base += 2;
    }

    for (const ActiveEdge& e : active)
    {
        DepArrowAnim& anim = tip_dep_anims_[e.key];
        anim.from_pos = e.from_pos;
        anim.to_pos = e.to_pos;
        anim.has_pos = true;
        anim.base_alpha = e.green ? kGreenArrowColor.a : kCyanArrowColor.a;
        anim.tip_scale = 1.f;

        // Cyan â†’ green color blend when the same edge key changes role.
        if (anim.want_green != e.green)
        {
            const float cur = (anim.confirm_blend_start_sec < 0.f)
                                  ? anim.confirm_blend_t
                                  : glm::mix(anim.confirm_blend_from, anim.want_green ? 1.f : 0.f,
                                             std::clamp((now - anim.confirm_blend_start_sec) /
                                                            kConfirmBlendSec,
                                                        0.f, 1.f));
            anim.confirm_blend_from = cur;
            anim.confirm_blend_start_sec = now;
            anim.want_green = e.green;
        }

        if (anim.birth_sec < 0.f)
        {
            anim.birth_sec = now + kArrowStagger * static_cast<float>(e.stagger_i);
            anim.phase = ArrowPhase::Growing;
            anim.confirm_blend_t = e.green ? 1.f : 0.f;
            anim.confirm_blend_start_sec = -1.f;
            anim.want_green = e.green;
        }
        else if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age >= kArrowGrowSec)
                anim.phase = ArrowPhase::Held;
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            // Edge returned to active set while dying â€” revive.
            anim.phase = ArrowPhase::Held;
            anim.fade_start_sec = 0.f;
        }
    }

    // Inactive / removed: red death if listing gone; erase otherwise (no red for role leave).
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        const std::string& key = it->first;
        DepArrowAnim& anim = it->second;
        std::string listing, dep;
        if (!parse_tip_edge_key(key, listing, dep))
        {
            it = tip_dep_anims_.erase(it);
            continue;
        }

        if (anim.phase == ArrowPhase::Dying)
        {
            ++it;
            continue;
        }

        const bool listing_live = live_nodes.count(listing) != 0;
        const bool dep_live = live_nodes.count(dep) != 0;

        if (!listing_live || !dep_live)
        {
            if (!listing_live && anim.has_pos)
            {
                anim.phase = ArrowPhase::Dying;
                anim.fade_start_sec = now;
                ++it;
                continue;
            }
            it = tip_dep_anims_.erase(it);
            continue;
        }

        if (active_keys.count(key) == 0)
        {
            it = tip_dep_anims_.erase(it);
            continue;
        }
        ++it;
    }

    uint32_t drawn = 0;
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        if (drawn >= kMaxDepArrows)
            break;
        DepArrowAnim& anim = it->second;
        if (!anim.has_pos)
        {
            ++it;
            continue;
        }

        glm::vec3 from_inset, to_inset;
        if (!inset_segment(anim.from_pos, anim.to_pos, clearance, from_inset, to_inset))
        {
            ++it;
            continue;
        }

        float grow_u = 1.f;
        float alpha = anim.base_alpha;

        // Advance confirm blend toward target.
        float blend = anim.confirm_blend_t;
        if (anim.confirm_blend_start_sec >= 0.f)
        {
            const float u = std::clamp((now - anim.confirm_blend_start_sec) / kConfirmBlendSec,
                                       0.f, 1.f);
            blend = glm::mix(anim.confirm_blend_from, anim.want_green ? 1.f : 0.f, u);
            anim.confirm_blend_t = blend;
            if (u >= 1.f)
                anim.confirm_blend_start_sec = -1.f;
        }
        else
        {
            blend = anim.want_green ? 1.f : 0.f;
            anim.confirm_blend_t = blend;
        }

        glm::vec4 color = glm::mix(kCyanArrowColor, kGreenArrowColor, blend);

        if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age <= 0.f)
            {
                ++it;
                continue;
            }
            grow_u = std::clamp(age / kArrowGrowSec, 0.f, 1.f);
            if (grow_u >= 1.f)
                anim.phase = ArrowPhase::Held;
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            color = kDeathArrowColor;
            const float t = std::clamp((now - anim.fade_start_sec) / kDeathSec, 0.f, 1.f);
            alpha = anim.base_alpha * (1.f - t);
            if (t >= 1.f)
            {
                it = tip_dep_anims_.erase(it);
                continue;
            }
        }

        color.a = alpha;
        debug.add_arrow(from_inset, to_inset, color,
                        tip_len * anim.tip_scale, tip_rad * anim.tip_scale,
                        shaft_r * anim.tip_scale, kDepRadial, grow_u);
        ++drawn;
        ++it;
    }
}

void ScenePresenter::update_death_and_walk_(
    const std::unordered_set<std::string>& live_nodes,
    const std::unordered_map<std::string, glm::vec3>& positions,
    float now)
{
    for (const std::string& h : prev_live_nodes_)
    {
        if (live_nodes.count(h) != 0)
            continue;
        bool already = false;
        for (const DyingBlock& d : dying_blocks_)
        {
            if (d.hash == h)
            {
                already = true;
                break;
            }
        }
        if (already)
            continue;
        DyingBlock db;
        db.hash = h;
        db.birth_sec = now;
        auto pit = prev_positions_.find(h);
        if (pit != prev_positions_.end())
            db.pos = pit->second;
        else
            continue;
        dying_blocks_.push_back(std::move(db));
    }

    for (auto it = dying_blocks_.begin(); it != dying_blocks_.end(); )
    {
        if (now - it->birth_sec >= kDeathSec)
            it = dying_blocks_.erase(it);
        else
            ++it;
    }

    // Green-tip walk animation along chain-walk path.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        auto path = scene_.frontier_walk_locked(static_cast<uint32_t>(lane));
        FrontierWalkAnim& w = walk_by_lane_[lane];
        if (path.size() >= 2)
        {
            if (w.path != path)
            {
                w.path = std::move(path);
                w.index = 0;
                w.step_start_sec = now;
            }
            scene_.clear_frontier_walk_locked(static_cast<uint32_t>(lane));
        }
        if (w.path.empty())
            continue;
        if (w.step_start_sec < 0.f)
            w.step_start_sec = now;
        while (w.index + 1 < w.path.size() &&
               (now - w.step_start_sec) >= kWalkStepSec)
        {
            ++w.index;
            w.step_start_sec = now;
        }
        if (w.index + 1 >= w.path.size() && (now - w.step_start_sec) >= kWalkStepSec)
            w.path.clear();
    }

    prev_live_nodes_ = live_nodes;
    prev_positions_ = positions;
}

void ScenePresenter::prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                             DebugDrawer* debug)
{
    out = FrameSourceOutput{};

    std::unique_lock<std::mutex> lock(scene_.mutex());

    const std::vector<GraphNode> graph_nodes = scene_.nodes_snapshot();

    // Shared timeline origin: earliest block timestamp (or now âˆ’ lookback).
    int64_t timeline_origin_ms = 0;
    {
        int64_t min_ts = 0;
        for (const GraphNode& n : graph_nodes)
        {
            if (n.timestamp_ms <= 0)
                continue;
            if (min_ts == 0 || n.timestamp_ms < min_ts)
                min_ts = n.timestamp_ms;
        }
        if (min_ts > 0)
            timeline_origin_ms = min_ts;
        else
            timeline_origin_ms = static_cast<int64_t>(std::time(nullptr)) * 1000 -
                                 static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
        scene_.set_timeline_origin_ms(timeline_origin_ms);
    }

    // Spacing unchanged: base_radius=20, meters_per_second=1, 16 lanes.
    LayoutParams layout_params;
    layout_params.meters_per_second = meters_per_second;
    layout_params.base_radius = kLayoutBaseRadius;
    layout_params.lane_count = 16;
    layout_params.timeline_origin_ms = timeline_origin_ms;

    LayoutResult layout = layout_.build(graph_nodes, layout_params);
    const auto& block_positions = layout.positions;

    // Network HUD (segments) — already under scene mutex.
    const BlockScene::NetworkHud hud = scene_.network_hud_locked();

    std::unordered_set<std::string> live_nodes;
    live_nodes.reserve(graph_nodes.size());
    for (const GraphNode& n : graph_nodes)
        live_nodes.insert(n.id);

    const float now = now_sec_();
    update_death_and_walk_(live_nodes, block_positions, now);

    // ------------------------------------------------------------------
    // Classify once (BlockFlow visual model)
    //   frontier_domain â€” domain confirmed tip hashes H_c (stable for cyan)
    //   green_display   â€” walk-anim tip or domain frontier (Sobel + green arrows)
    //   cyan_owners     â€” unconfirmed height>H_c that deps a domain frontier tip
    //   incomplete      â€” missing live deps, excluding green/cyan
    //   solid           â€” confirmed âˆ§ complete
    // ------------------------------------------------------------------
    const std::vector<NodeId> frontier_ids = scene_.confirmed_frontier_ids_locked();
    std::unordered_set<std::string> frontier_domain;
    frontier_domain.reserve(frontier_ids.size() * 2 + 1);
    for (const NodeId& h : frontier_ids)
        if (!h.empty())
            frontier_domain.insert(h);

    std::unordered_set<std::string> green_display;
    green_display.reserve(16);
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        const FrontierWalkAnim& w = walk_by_lane_[lane];
        if (!w.path.empty() && w.index < w.path.size())
            green_display.insert(w.path[w.index]);
    }
    if (green_display.empty())
        green_display = frontier_domain;
    // Uncles must never be green tips.
    for (auto it = green_display.begin(); it != green_display.end(); )
    {
        if (scene_.is_uncle_locked(*it))
            it = green_display.erase(it);
        else
            ++it;
    }

    int hc_by_lane[BlockScene::kLaneCount];
    scene_.copy_confirmed_heights_locked(hc_by_lane);

    std::unordered_set<std::string> cyan_owners;
    cyan_owners.reserve(32);
    for (const GraphNode& n : graph_nodes)
    {
        if (n.id.empty() || live_nodes.count(n.id) == 0)
            continue;
        if (scene_.is_confirmed_locked(n.id) || scene_.is_uncle_locked(n.id) ||
            n.role == BlockRole::Uncle)
            continue;
        if (n.lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
            continue;
        const int hc = hc_by_lane[n.lane];
        if (hc < 0 || n.height <= hc)
            continue;

        auto d = scene_.detail_store().get(n.id);
        if (!d)
            continue;
        bool refs_frontier = false;
        for (const std::string& dep : d->deps)
        {
            if (!dep.empty() && frontier_domain.count(dep) != 0)
            {
                refs_frontier = true;
                break;
            }
        }
        if (refs_frontier)
            cyan_owners.insert(n.id);
    }

    std::unordered_map<std::string, bool> missing_dep;
    missing_dep.reserve(graph_nodes.size());
    std::vector<std::string> incomplete_pool;
    incomplete_pool.reserve(64);
    for (const GraphNode& n : graph_nodes)
    {
        bool missing = false;
        if (auto d = scene_.detail_store().get(n.id))
        {
            for (const std::string& dep : d->deps)
            {
                if (dep.empty())
                    continue;
                if (live_nodes.count(dep) == 0)
                {
                    missing = true;
                    break;
                }
            }
        }
        missing_dep[n.id] = missing;
        if (missing && green_display.count(n.id) == 0 && cyan_owners.count(n.id) == 0)
            incomplete_pool.push_back(n.id);
    }

    auto is_solid = [&](const std::string& h) -> bool {
        if (!scene_.is_confirmed_locked(h))
            return false;
        auto it = missing_dep.find(h);
        return !(it != missing_dep.end() && it->second);
    };

    if (!in.selected_hash.empty())
    {
        auto lit = block_positions.find(in.selected_hash);
        if (lit != block_positions.end())
        {
            out.has_look_target = true;
            out.look_target_pos = lit->second;
        }
    }

    out.instances.reserve(layout.placements.size());
    out.pick_map.reserve(layout.placements.size());
    // Slightly higher on dark canvas so unconfirmed cubes remain readable.
    constexpr float kUnconfirmedAlpha = 0.48f;
    const float scale = kDefaultBlockScale;

    // ------------------------------------------------------------------
    // Hierarchical cull: at most the newest 4 segments, then frustum slabs
    // → blocks. Hard bound keeps instance work in a fixed lookback range.
    // ------------------------------------------------------------------
    constexpr int kMaxVisibleTimeSegments = 4;
    const int nseg = std::clamp(hud.segment_count, 0, BlockScene::kMaxTimeSegments);
    const int n_eligible = std::min(nseg, kMaxVisibleTimeSegments);
    const float eye_z = scene_.camera_scroll_z();
    const float R_cull = kLayoutBaseRadius * 1.35f;
    float seg_width_z = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS) * meters_per_second;
    if (nseg > 0)
    {
        const int64_t span_ms =
            std::max<int64_t>(1, hud.segments[0].to_ms - hud.segments[0].from_ms);
        seg_width_z = static_cast<float>(span_ms) * 0.001f * meters_per_second;
    }

    bool seg_visible[BlockScene::kMaxTimeSegments]{};
    float vis_z_lo = 0.f;
    float vis_z_hi = 0.f;
    bool  have_vis_z = false;
    int   visible_seg_count = 0;

    auto mark_vis_z = [&](float z0, float z1) {
        const float lo = std::min(z0, z1);
        const float hi = std::max(z0, z1);
        if (!have_vis_z)
        {
            vis_z_lo = lo;
            vis_z_hi = hi;
            have_vis_z = true;
        }
        else
        {
            vis_z_lo = std::min(vis_z_lo, lo);
            vis_z_hi = std::max(vis_z_hi, hi);
        }
    };

    if (n_eligible > 0)
    {
        // Index 0 = live (newest). Only i in [0, n_eligible) may draw.
        for (int i = 0; i < n_eligible; ++i)
        {
            const auto& s = hud.segments[i];
            if (s.from_ms <= 0 && s.to_ms <= 0)
                continue;
            const float z_from = ts_to_z(s.from_ms, timeline_origin_ms, meters_per_second);
            const float z_to   = ts_to_z(s.to_ms, timeline_origin_ms, meters_per_second);
            const float z_lo   = std::min(z_from, z_to);
            const float z_hi   = std::max(z_from, z_to);
            const glm::vec3 center(0.f, 0.f, 0.5f * (z_lo + z_hi));
            const glm::vec3 half(R_cull, R_cull, 0.5f * (z_hi - z_lo) + 2.f);
            if (in.frustum && !in.frustum->intersects_aabb(center, half))
                continue;
            seg_visible[i] = true;
            ++visible_seg_count;
            mark_vis_z(z_lo, z_hi);
        }
        // Safety: if frustum culled all eligible, keep nearest among last 4.
        if (visible_seg_count == 0)
        {
            int best = 0;
            float best_d = 1e30f;
            for (int i = 0; i < n_eligible; ++i)
            {
                const auto& s = hud.segments[i];
                const float z_from = ts_to_z(s.from_ms, timeline_origin_ms, meters_per_second);
                const float z_to   = ts_to_z(s.to_ms, timeline_origin_ms, meters_per_second);
                const float mid    = 0.5f * (z_from + z_to);
                const float d      = std::abs(mid - eye_z);
                if (d < best_d)
                {
                    best_d = d;
                    best = i;
                }
            }
            seg_visible[best] = true;
            visible_seg_count = 1;
            const auto& s = hud.segments[best];
            mark_vis_z(ts_to_z(s.from_ms, timeline_origin_ms, meters_per_second),
                       ts_to_z(s.to_ms, timeline_origin_ms, meters_per_second));
        }
    }

    auto block_in_visible_segment = [&](int64_t ts_ms) -> bool {
        if (n_eligible <= 0)
            return true; // no segment model yet → draw all (legacy path)
        if (ts_ms <= 0)
            return true;
        for (int i = 0; i < n_eligible; ++i)
        {
            if (!seg_visible[i])
                continue;
            const auto& s = hud.segments[i];
            if (ts_ms >= s.from_ms && ts_ms < s.to_ms)
                return true;
        }
        return false;
    };

    auto passes_txn_filter = [&](const PlacedBlock& placed) -> bool {
        if (!in.filter_txn_gt_1)
            return true;
        // Strict: only known multi-tx blocks (unknown -1 is hidden).
        return placed.txn_count > 1;
    };

    auto push_instance = [&](const PlacedBlock& placed, bool force) {
        if (out.instances.size() >= kMaxInstances)
            return false;
        const glm::vec3 half = in.instance_half_extents * scale;
        if (!force && in.frustum && !in.frustum->intersects_aabb(placed.pos, half))
            return false;

        glm::vec3 color = placed.color;
        if (!in.selected_hash.empty() && placed.hash == in.selected_hash)
            color = glm::mix(color, glm::vec3(1.f, 1.f, 1.f), 0.45f);
        else if (!in.hovered_hash.empty() && placed.hash == in.hovered_hash)
            color = glm::mix(color, glm::vec3(1.f, 0.9f, 0.4f), 0.35f);

        float alpha = is_solid(placed.hash) ? 1.0f : kUnconfirmedAlpha;
        if (placed.is_uncle || scene_.is_uncle_locked(placed.hash))
            alpha = std::min(alpha, 0.55f); // uncles always slightly translucent
        out.instances.push_back(GpuInstance{ placed.pos, scale, color, alpha });
        out.pick_map.push_back(placed.hash);
        return true;
    };

    std::unordered_set<std::string> drawn;
    for (const PlacedBlock& placed : layout.placements)
    {
        if (!block_in_visible_segment(placed.timestamp_ms))
            continue;
        if (!passes_txn_filter(placed))
            continue;
        if (push_instance(placed, /*force=*/false))
            drawn.insert(placed.hash);
    }

    // Force-draw highlight roles so Sobel can resolve them — only if segment
    // is in the draw set (or selection/hover, always).
    for (const PlacedBlock& placed : layout.placements)
    {
        if (drawn.count(placed.hash))
            continue;
        const bool is_sel =
            (!in.selected_hash.empty() && placed.hash == in.selected_hash) ||
            (!in.hovered_hash.empty() && placed.hash == in.hovered_hash);
        if (!is_sel && !block_in_visible_segment(placed.timestamp_ms))
            continue;
        // Multi-tx filter: still force selected/hovered; other force roles must pass filter.
        if (!is_sel && !passes_txn_filter(placed))
            continue;
        const bool force = is_sel || missing_dep[placed.hash] ||
                           green_display.count(placed.hash) ||
                           cyan_owners.count(placed.hash);
        if (!force)
            continue;
        if (push_instance(placed, /*force=*/true))
            drawn.insert(placed.hash);
    }

    // Dynamic near/far from visible segment Z span (+ lateral pad).
    {
        constexpr float kHardFarCap = 20000.f;
        constexpr float kMinNear    = 0.5f;
        float near_z = Camera::kDefaultNearZ;
        float far_z  = Camera::kDefaultFarZ;
        if (have_vis_z)
        {
            const float z_dist =
                std::max(std::abs(vis_z_hi - eye_z), std::abs(vis_z_lo - eye_z));
            const float lat_pad = R_cull + 40.f;
            const float pad     = std::max(seg_width_z * 0.35f, 40.f);
            near_z = kMinNear;
            far_z  = std::min(kHardFarCap, std::max(near_z + 50.f, z_dist + lat_pad + pad));
        }
        else if (!layout.placements.empty())
        {
            // Fallback: span of all placements.
            float z_lo = layout.placements[0].pos.z;
            float z_hi = z_lo;
            for (const PlacedBlock& p : layout.placements)
            {
                z_lo = std::min(z_lo, p.pos.z);
                z_hi = std::max(z_hi, p.pos.z);
            }
            const float z_dist =
                std::max(std::abs(z_hi - eye_z), std::abs(z_lo - eye_z));
            near_z = kMinNear;
            far_z  = std::min(kHardFarCap, std::max(near_z + 50.f, z_dist + R_cull + 80.f));
        }
        out.has_clip_suggestion = true;
        out.suggested_near_z    = near_z;
        out.suggested_far_z     = far_z;
    }

    for (const DyingBlock& db : dying_blocks_)
    {
        if (out.instances.size() >= kMaxInstances)
            break;
        const float t = std::clamp((now - db.birth_sec) / kDeathSec, 0.f, 1.f);
        const float alpha = (1.f - t) * 0.9f;
        out.instances.push_back(
            GpuInstance{ db.pos, scale, glm::vec3(1.f, 0.12f, 0.1f), alpha });
        out.pick_map.push_back(db.hash);
    }

    // Sobel lists: green tips + cyan frontier children + orange incompletes.
    {
        std::unordered_set<std::string> pick_set(out.pick_map.begin(), out.pick_map.end());

        for (const std::string& h : green_display)
        {
            if (h.empty() || !pick_set.count(h))
                continue;
            out.confirmed_tip_hashes.push_back(h);
        }

        for (const std::string& h : cyan_owners)
        {
            if (pick_set.count(h))
                out.cyan_frontier_hashes.push_back(h);
        }

        std::unordered_set<std::string> highlight_set(out.confirmed_tip_hashes.begin(),
                                                     out.confirmed_tip_hashes.end());
        for (const auto& h : out.cyan_frontier_hashes)
            highlight_set.insert(h);
        for (const auto& h : incomplete_pool)
        {
            if (!pick_set.count(h) || highlight_set.count(h))
                continue;
            out.incomplete_hashes.push_back(h);
        }
    }

    if (debug)
    {
        const float tip_len = std::max(0.18f, meters_per_second * 0.08f * 8.f);
        const float tip_rad = std::max(0.06f, meters_per_second * 0.03f * 8.f);
        const float shaft_r = tip_rad * 0.4f;
        const float clearance = std::max(0.55f, meters_per_second * 0.12f * 8.f);
        constexpr uint32_t kDepRadial = 8;
        constexpr uint32_t kMaxDepArrows = 512;

        tip_dep_tick_and_draw_(*debug, block_positions, live_nodes, green_display, cyan_owners,
                               frontier_domain, tip_len, tip_rad, shaft_r, clearance);

        uint32_t arrow_count = 0;
        std::unordered_set<std::string> eph_seen;

        auto eph_key = [](char kind, const std::string& from, const std::string& to) {
            return std::string(1, kind) + '|' + from + '|' + to;
        };

        auto add_eph_arrow = [&](char kind, const std::string& from_hash,
                                 const std::string& to_hash,
                                 const glm::vec3& from, const glm::vec3& to,
                                 const glm::vec4& color, float tip_scale, int stagger_i) {
            if (arrow_count >= kMaxDepArrows)
                return;
            glm::vec3 from_inset, to_inset;
            if (!inset_segment(from, to, clearance, from_inset, to_inset))
                return;
            const std::string key = eph_key(kind, from_hash, to_hash);
            const float grow = ephemeral_grow_u_(key, kArrowStagger * static_cast<float>(stagger_i),
                                                 eph_seen);
            if (grow <= 0.f)
                return;
            debug->add_arrow(from_inset, to_inset, color,
                             tip_len * tip_scale, tip_rad * tip_scale,
                             shaft_r * tip_scale, kDepRadial, grow);
            ++arrow_count;
        };

        const glm::vec4 kMissingOutline(0.75f, 0.75f, 0.8f, 0.9f);
        const float ghost_half = 1.0f;

        // Placement colors for dep-hover recolor (inspector Deps row).
        std::unordered_map<std::string, glm::vec3> placement_color;
        placement_color.reserve(layout.placements.size());
        for (const PlacedBlock& p : layout.placements)
            placement_color[p.hash] = p.color;

        auto draw_selection_deps = [&](const AlphBlock& block) {
            auto listing_it = block_positions.find(block.hash);
            if (listing_it == block_positions.end())
                return;
            const glm::vec3& listing_pos = listing_it->second;
            const int parent_lane = block.chain_idx();
            int missing_i = 0;
            int stagger_i = 0;
            const bool any_ui_dep_hover = !in.ui_dep_hover_hash.empty();

            for (const std::string& dep_hash : block.deps)
            {
                auto dep_it = block_positions.find(dep_hash);
                if (dep_it != block_positions.end())
                {
                    const bool focus =
                        any_ui_dep_hover && dep_hash == in.ui_dep_hover_hash;
                    glm::vec4 arrow_col = kSelectionArrowColor;
                    if (focus)
                    {
                        auto cit = placement_color.find(dep_hash);
                        const glm::vec3 c = (cit != placement_color.end())
                                                ? cit->second
                                                : glm::vec3(arrow_col);
                        arrow_col = glm::vec4(c, 1.f);
                    }
                    else if (any_ui_dep_hover)
                    {
                        // Dim non-focused selection edges while inspecting one dep.
                        arrow_col.a = 0.35f;
                    }
                    add_eph_arrow('s', block.hash, dep_hash, listing_pos, dep_it->second,
                                  arrow_col, focus ? 1.28f : 1.15f, stagger_i++);
                    continue;
                }

                const float angle =
                    ((static_cast<float>(parent_lane) + 0.35f +
                      0.08f * static_cast<float>(missing_i)) /
                     16.0f) *
                    2.0f * 3.14159265f;
                const float radius = 20.0f * 0.9f;
                glm::vec3 ghost(
                    radius * std::cos(angle),
                    radius * std::sin(angle),
                    listing_pos.z + meters_per_second *
                                        static_cast<float>(ALPH_TARGET_BLOCK_SECONDS));
                const bool focus_missing =
                    any_ui_dep_hover && dep_hash == in.ui_dep_hover_hash;
                const glm::vec4 ghost_col =
                    focus_missing ? glm::vec4(1.f, 0.55f, 0.2f, 0.95f) : kMissingOutline;
                debug->add_wire_box(ghost, ghost_half, ghost_col);
                debug->add_line(listing_pos, ghost, ghost_col);
                ++missing_i;
            }
        };

        if (!in.selected_hash.empty() && in.selected_detail.hash == in.selected_hash)
            draw_selection_deps(in.selected_detail);
        else if (!in.selected_hash.empty())
        {
            if (auto d = scene_.detail_store().get(in.selected_hash))
                draw_selection_deps(*d);
        }

        if (!in.hovered_hash.empty() && in.hovered_hash != in.selected_hash)
        {
            if (auto d = scene_.detail_store().get(in.hovered_hash))
            {
                auto listing_it = block_positions.find(d->hash);
                if (listing_it != block_positions.end())
                {
                    int stagger_i = 0;
                    for (const std::string& dep_hash : d->deps)
                    {
                        auto dep_it = block_positions.find(dep_hash);
                        if (dep_it == block_positions.end())
                            continue;
                        add_eph_arrow('h', d->hash, dep_hash, listing_it->second, dep_it->second,
                                      kHoverArrowColor, 1.05f, stagger_i++);
                    }
                }
            }
        }

        for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end(); )
        {
            if (eph_seen.find(it->first) == eph_seen.end())
                it = ephemeral_birth_sec_.erase(it);
            else
                ++it;
        }

        // Progressive barrier plane: only the past edge of the segment the camera is
        // still "in" (most recent uncrossed boundary). Next older plane appears only
        // after the eye crosses this one. Blocks still use multi-segment cull above.
        if (n_eligible > 0)
        {
            const float plane_half = kLayoutBaseRadius * 8.f;
            // Hysteresis so the plane does not flicker when eye sits on the boundary.
            const float cross_eps = std::max(0.5f, seg_width_z * 0.02f);
            float plane_z = 0.f;
            bool  have_plane = false;
            for (int i = 0; i < n_eligible; ++i)
            {
                const auto& s = hud.segments[i];
                if (s.from_ms <= 0 && s.to_ms <= 0)
                    continue;
                const float z_from = ts_to_z(s.from_ms, timeline_origin_ms, meters_per_second);
                const float z_to   = ts_to_z(s.to_ms, timeline_origin_ms, meters_per_second);
                // Layout: older = larger Z → past boundary is max(z_from, z_to).
                const float past_z = std::max(z_from, z_to);
                // Still on the live/newer side of this past edge → draw it and stop.
                if (eye_z <= past_z + cross_eps)
                {
                    plane_z = past_z;
                    have_plane = true;
                    break;
                }
            }
            if (have_plane)
                debug->add_z_plane_quad(plane_z, plane_half, kBarrierPlaneColor);
        }
    }

    out.ui.total_blocks = scene_.total_blocks();
    out.ui.selected_hash = in.selected_hash;
    out.ui.selected_detail = in.selected_detail;
    scene_.get_trace_status_locked(&out.ui.trace_phase, &out.ui.trace_offset);

    // Hover billboard: content + world anchor (overlay fades / projects).
    // Policy A: suppress while hovered == selected (inspector owns the block).
    {
        auto& bb = out.ui.block_billboard;
        bb = UiSnapshot::BlockBillboardUi{};
        const bool hover_ok = !in.hovered_hash.empty() &&
                              (in.selected_hash.empty() || in.hovered_hash != in.selected_hash);
        if (hover_ok)
        {
            auto pit = block_positions.find(in.hovered_hash);
            if (pit != block_positions.end())
            {
                const glm::vec3 block_pos = pit->second;
                glm::vec3 eye = in.has_camera_eye
                                    ? in.camera_eye
                                    : glm::vec3(0.f, 0.f, scene_.camera_scroll_z());
                glm::vec3 to_cam = eye - block_pos;
                const float len = glm::length(to_cam);
                if (len > 1e-4f)
                    to_cam /= len;
                else
                    to_cam = glm::vec3(0.f, 0.f, 1.f);
                constexpr float kBillboardOffset = 2.5f;
                const glm::vec3 world = block_pos + to_cam * kBillboardOffset;

                bb.want_visible = true;
                std::snprintf(bb.hash, sizeof(bb.hash), "%s", in.hovered_hash.c_str());
                bb.world_pos[0] = world.x;
                bb.world_pos[1] = world.y;
                bb.world_pos[2] = world.z;

                // Prefer placement height/lane/txn_count; fall back to detail store.
                int height = -1;
                int chain_from = -1;
                int chain_to = -1;
                int txn_count = -1;
                bool is_uncle = scene_.is_uncle_locked(in.hovered_hash);
                for (const PlacedBlock& p : layout.placements)
                {
                    if (p.hash != in.hovered_hash)
                        continue;
                    height = p.height;
                    chain_from = static_cast<int>(p.lane / ALPH_NUM_GROUPS);
                    chain_to = static_cast<int>(p.lane % ALPH_NUM_GROUPS);
                    txn_count = p.txn_count;
                    is_uncle = is_uncle || p.is_uncle;
                    break;
                }
                if (auto d = scene_.detail_store().get(in.hovered_hash))
                {
                    if (height < 0)
                        height = d->height;
                    if (chain_from < 0)
                    {
                        chain_from = d->chainFrom;
                        chain_to = d->chainTo;
                    }
                    // Prefer preserved parse-time count (survives slim); not txns.size().
                    if (d->txn_count >= 0)
                        txn_count = d->txn_count;
                    else if (txn_count < 0 && !d->txns.empty())
                        txn_count = static_cast<int>(d->txns.size());
                }
                bb.height = height;
                bb.chain_from = chain_from;
                bb.chain_to = chain_to;
                bb.txn_count = txn_count;
                bb.is_uncle = is_uncle ? 1 : 0;
            }
        }
    }
    {
        const auto tips = scene_.tip_ids();
        out.ui.tip_count = static_cast<int>(tips.size());
        out.ui.confirmed_tip_count = static_cast<int>(frontier_ids.size());
        scene_.copy_confirmed_heights_locked(out.ui.confirmed_height_by_lane);
    }
    {
        // prepare already holds scene_.mutex() — do not call network_hud() (re-lock = deadlock).
        // hud was captured earlier under the same lock for culling/planes.
        out.ui.net_domain = hud.domain;
        out.ui.net_status = hud.status;
        std::snprintf(out.ui.net_base_url, sizeof(out.ui.net_base_url), "%s", hud.base_url);
        out.ui.lookback_windows_done = hud.lookback_windows_done;
        out.ui.lookback_windows_need = hud.lookback_windows_need;
        out.ui.lanes_with_frontier = hud.lanes_with_frontier;
        out.ui.open_confirm_walks = hud.open_confirm_walks;
        for (int i = 0; i < 16; ++i)
            out.ui.tip_height_by_lane[i] = hud.tip_height_by_lane[i];
        out.ui.stats_api_is_main = hud.stats_api_is_main;
        out.ui.stats_fetch_admitted = hud.stats_fetch_admitted;
        out.ui.stats_removed = hud.stats_removed;
        out.ui.stats_seed_q = hud.stats_seed_q;
        out.ui.last_poll_ms = hud.last_poll_ms;
        out.ui.poll_interval_sec = hud.poll_interval_sec;
        out.ui.net_switching = hud.switching;
        out.ui.segment_count =
            std::clamp(hud.segment_count, 0, UiSnapshot::kMaxTimeSegments);
        for (int i = 0; i < out.ui.segment_count; ++i)
        {
            const auto& s = hud.segments[i];
            auto& d = out.ui.segments[i];
            d.index = s.index;
            d.from_ms = s.from_ms;
            d.to_ms = s.to_ms;
            d.load_ratio = s.load_ratio;
            d.confirmed_full = s.confirmed_full;
            d.block_count = s.block_count;
            d.expected_blocks = s.expected_blocks;
        }
    }
    for (const RecentFeedItem& b : scene_.feed())
    {
        FeedEntry e;
        e.hash = b.hash;
        e.chainFrom = b.chainFrom;
        e.chainTo = b.chainTo;
        e.height = b.height;
        out.ui.feed.push_back(std::move(e));
    }
}
