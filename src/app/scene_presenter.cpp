#include "app/scene_presenter.hpp"

#include "alph_block.hpp"
#include "graphics/debug/debug_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/glm.hpp>

namespace
{
float meters_per_height = static_cast<float>(ALPH_TARGET_BLOCK_SECONDS);

const glm::vec4 kActiveArrowColor(0.15f, 0.95f, 1.0f, 0.95f);    // cyan unconfirmed
const glm::vec4 kConfirmedArrowColor(0.20f, 0.95f, 0.35f, 0.95f); // green main-chain tip
constexpr float kConfirmBlendSec = 0.35f; // cyan→green lerp duration
const glm::vec4 kSelectionArrowColor(1.0f, 0.85f, 0.2f, 1.0f);
const glm::vec4 kHoverArrowColor(1.0f, 0.85f, 0.2f, 0.45f);

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

std::string tip_edge_key(const std::string& from, const std::string& to)
{
    return std::string("t|") + from + '|' + to;
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
    float tip_len, float tip_rad, float shaft_r, float clearance)
{
    const float now = now_sec_();
    constexpr uint32_t kDepRadial = 8;
    constexpr uint32_t kMaxDepArrows = 512;

    // Active tip → dep edges this frame.
    std::unordered_set<std::string> active_keys;
    active_keys.reserve(64);

    struct ActiveEdge
    {
        std::string key;
        std::string from;
        std::string to;
        glm::vec3 from_pos{};
        glm::vec3 to_pos{};
        int stagger_i = 0;
    };
    std::vector<ActiveEdge> active;
    active.reserve(64);

    int tip_stagger_base = 0;
    for (const NodeId& tip_hash : scene_.tip_ids())
    {
        auto d = scene_.detail_store().get(tip_hash);
        if (!d)
            continue;
        auto to_it = positions.find(tip_hash);
        if (to_it == positions.end())
            continue;

        int stagger_i = 0;
        for (const std::string& dep_hash : d->deps)
        {
            auto from_it = positions.find(dep_hash);
            if (from_it == positions.end())
                continue;
            ActiveEdge e;
            e.key = tip_edge_key(dep_hash, tip_hash);
            e.from = dep_hash;
            e.to = tip_hash;
            e.from_pos = from_it->second;
            e.to_pos = to_it->second;
            e.stagger_i = tip_stagger_base + stagger_i++;
            active_keys.insert(e.key);
            active.push_back(std::move(e));
        }
        tip_stagger_base += 2; // slight global cascade across tips
    }

    // Admit / refresh active edges (Growing/Held). Confirm blend updates only here;
    // Fading/Gone freeze last confirm_blend_t. Never reset birth_sec for confirm.
    for (const ActiveEdge& e : active)
    {
        DepArrowAnim& anim = tip_dep_anims_[e.key];
        anim.from_pos = e.from_pos;
        anim.to_pos = e.to_pos;
        anim.has_pos = true;
        anim.base_alpha = kActiveArrowColor.a;
        anim.tip_scale = 1.f;

        // Cyan→green blend toward scene confirmation of tip endpoint (mutex held by prepare).
        const bool conf = scene_.is_confirmed_locked(e.to);
        if (conf != anim.tip_confirmed)
        {
            anim.tip_confirmed = conf;
            anim.confirm_blend_from = anim.confirm_blend_t;
            anim.confirm_blend_start_sec = now;
        }
        const float target = conf ? 1.f : 0.f;
        if (anim.confirm_blend_start_sec >= 0.f)
        {
            const float u = std::clamp(
                (now - anim.confirm_blend_start_sec) / kConfirmBlendSec, 0.f, 1.f);
            anim.confirm_blend_t = glm::mix(anim.confirm_blend_from, target, u);
            if (u >= 1.f)
                anim.confirm_blend_start_sec = -1.f;
        }
        else
        {
            anim.confirm_blend_t = target;
        }

        if (anim.phase == ArrowPhase::Gone || anim.phase == ArrowPhase::Fading)
        {
            // Tip active again: full length, no re-grow.
            anim.phase = ArrowPhase::Held;
            anim.fade_start_sec = 0.f;
            if (anim.birth_sec < 0.f)
                anim.birth_sec = now; // never grew (edge case)
            continue;
        }

        if (anim.birth_sec < 0.f)
        {
            // New edge: start grow with stagger (one dep at a time).
            anim.birth_sec = now + kArrowStagger * static_cast<float>(e.stagger_i);
            anim.phase = ArrowPhase::Growing;
        }
        else if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age >= kArrowGrowSec)
                anim.phase = ArrowPhase::Held;
        }
        // Held: stay
    }

    // Edges no longer active tips: fade if nodes still live; erase if removed from graph.
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        const std::string& key = it->first;
        DepArrowAnim& anim = it->second;

        // Parse from|to from key "t|from|to"
        std::string from, to;
        {
            const size_t p0 = key.find('|');
            const size_t p1 = (p0 == std::string::npos) ? std::string::npos : key.find('|', p0 + 1);
            if (p0 != std::string::npos && p1 != std::string::npos)
            {
                from = key.substr(p0 + 1, p1 - p0 - 1);
                to = key.substr(p1 + 1);
            }
        }

        const bool live = !from.empty() && !to.empty() &&
                          live_nodes.count(from) && live_nodes.count(to);

        if (!live)
        {
            it = tip_dep_anims_.erase(it);
            continue;
        }

        if (active_keys.count(key) == 0)
        {
            if (anim.phase == ArrowPhase::Growing || anim.phase == ArrowPhase::Held)
            {
                anim.phase = ArrowPhase::Fading;
                anim.fade_start_sec = now;
                // Keep full length during fade (grow_u = 1).
            }
            else if (anim.phase == ArrowPhase::Fading)
            {
                const float fade_age = now - anim.fade_start_sec;
                if (fade_age >= kArrowFadeSec)
                    anim.phase = ArrowPhase::Gone;
            }
            // Gone: keep entry until node removal so re-tip doesn't re-grow
        }
        ++it;
    }

    // Draw Growing / Held / Fading (not Gone).
    uint32_t drawn = 0;
    for (auto& kv : tip_dep_anims_)
    {
        if (drawn >= kMaxDepArrows)
            break;
        DepArrowAnim& anim = kv.second;
        if (anim.phase == ArrowPhase::Gone || !anim.has_pos)
            continue;

        glm::vec3 from_inset, to_inset;
        if (!inset_segment(anim.from_pos, anim.to_pos, clearance, from_inset, to_inset))
            continue;

        float grow_u = 1.f;
        float alpha = anim.base_alpha;

        if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age <= 0.f)
                continue;
            grow_u = std::clamp(age / kArrowGrowSec, 0.f, 1.f);
            if (grow_u >= 1.f)
                anim.phase = ArrowPhase::Held;
        }
        else if (anim.phase == ArrowPhase::Fading)
        {
            grow_u = 1.f; // full length — alpha only
            const float fade_age = now - anim.fade_start_sec;
            const float t = std::clamp(fade_age / kArrowFadeSec, 0.f, 1.f);
            alpha = anim.base_alpha * (1.f - t);
            if (t >= 1.f)
            {
                anim.phase = ArrowPhase::Gone;
                continue;
            }
        }

        glm::vec4 color = glm::mix(kActiveArrowColor, kConfirmedArrowColor, anim.confirm_blend_t);
        color.a = alpha;
        debug.add_arrow(from_inset, to_inset, color,
                        tip_len * anim.tip_scale, tip_rad * anim.tip_scale,
                        shaft_r * anim.tip_scale, kDepRadial, grow_u);
        ++drawn;
    }
}

void ScenePresenter::prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                             DebugDrawer* debug)
{
    out = FrameSourceOutput{};

    std::unique_lock<std::mutex> lock(scene_.mutex());

    LayoutParams layout_params;
    layout_params.meters_per_height = meters_per_height;
    layout_params.base_radius = 20.0f;
    layout_params.lane_count = 16;

    const std::vector<GraphNode> graph_nodes = scene_.nodes_snapshot();
    LayoutResult layout = layout_.build(graph_nodes, layout_params);
    const auto& block_positions = layout.positions;

    std::unordered_set<std::string> live_nodes;
    live_nodes.reserve(graph_nodes.size());
    for (const GraphNode& n : graph_nodes)
        live_nodes.insert(n.id);

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
    for (const PlacedBlock& placed : layout.placements)
    {
        if (out.instances.size() >= kMaxInstances)
        {
            std::printf("Instance buffer full\n");
            break;
        }

        const float scale = kDefaultBlockScale;
        const glm::vec3 half = in.instance_half_extents * scale;

        if (in.frustum && !in.frustum->intersects_aabb(placed.pos, half))
            continue;

        glm::vec3 color = placed.color;
        if (!in.selected_hash.empty() && placed.hash == in.selected_hash)
            color = glm::mix(color, glm::vec3(1.f, 1.f, 1.f), 0.45f);
        else if (!in.hovered_hash.empty() && placed.hash == in.hovered_hash)
            color = glm::mix(color, glm::vec3(1.f, 0.9f, 0.4f), 0.35f);

        out.instances.push_back(GpuInstance{ placed.pos, scale, color, 1.0f });
        out.pick_map.push_back(placed.hash);
    }

    // Confirmed tips ∩ this frame's pick_map (frustum-culled tips omitted). Cap 32.
    {
        const auto conf_tips = scene_.confirmed_tip_ids_locked();
        std::unordered_set<std::string> conf_set(conf_tips.begin(), conf_tips.end());
        for (const auto& h : out.pick_map)
        {
            if (!conf_set.count(h))
                continue;
            out.confirmed_tip_hashes.push_back(h);
            if (out.confirmed_tip_hashes.size() >= 32)
                break;
        }
    }

    if (debug)
    {
        const float tip_len = std::max(0.18f, meters_per_height * 0.08f);
        const float tip_rad = std::max(0.06f, meters_per_height * 0.03f);
        const float shaft_r = tip_rad * 0.4f;
        const float clearance = std::max(0.55f, meters_per_height * 0.12f);
        constexpr uint32_t kDepRadial = 8;
        constexpr uint32_t kMaxDepArrows = 512;

        // Active tip deps: grow once → hold → fade on confirm (no false re-grow).
        tip_dep_tick_and_draw_(*debug, block_positions, live_nodes,
                               tip_len, tip_rad, shaft_r, clearance);

        // Selection / hover: ephemeral grow (separate keys).
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

        auto draw_selection_deps = [&](const AlphBlock& block) {
            auto to_it = block_positions.find(block.hash);
            if (to_it == block_positions.end())
                return;
            const glm::vec3& to_pos = to_it->second;
            const int parent_lane = block.chain_idx();
            int missing_i = 0;
            int stagger_i = 0;

            for (const std::string& dep_hash : block.deps)
            {
                auto from_it = block_positions.find(dep_hash);
                if (from_it != block_positions.end())
                {
                    add_eph_arrow('s', dep_hash, block.hash, from_it->second, to_pos,
                                  kSelectionArrowColor, 1.15f, stagger_i++);
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
                    to_pos.z + meters_per_height);
                debug->add_wire_box(ghost, ghost_half, kMissingOutline);
                debug->add_line(to_pos, ghost, kMissingOutline);
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
                auto to_it = block_positions.find(d->hash);
                if (to_it != block_positions.end())
                {
                    int stagger_i = 0;
                    for (const std::string& dep_hash : d->deps)
                    {
                        auto from_it = block_positions.find(dep_hash);
                        if (from_it == block_positions.end())
                            continue;
                        add_eph_arrow('h', dep_hash, d->hash, from_it->second, to_it->second,
                                      kHoverArrowColor, 1.05f, stagger_i++);
                    }
                }
            }
        }

        // Ephemeral birth times only — tip deps never reset here.
        for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end(); )
        {
            if (eph_seen.find(it->first) == eph_seen.end())
                it = ephemeral_birth_sec_.erase(it);
            else
                ++it;
        }
    }

    out.ui.total_blocks = scene_.total_blocks();
    out.ui.selected_hash = in.selected_hash;
    out.ui.selected_detail = in.selected_detail;
    {
        // HUD tip counts (mutex already held); same tip set as tip_dep / Sobel sources.
        const auto tips = scene_.tip_ids();
        out.ui.tip_count = static_cast<int>(tips.size());
        int confirmed = 0;
        for (const NodeId& id : tips)
        {
            if (scene_.is_confirmed_locked(id))
                ++confirmed;
        }
        out.ui.confirmed_tip_count = confirmed;
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
