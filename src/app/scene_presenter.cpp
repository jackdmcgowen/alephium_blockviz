#include "app/scene_presenter.hpp"

#include "alph_block.hpp"
#include "graphics/debug/debug_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include <glm/glm.hpp>

namespace
{
float meters_per_height = static_cast<float>(ALPH_TARGET_BLOCK_SECONDS);

const glm::vec4 kActiveArrowColor(0.15f, 0.95f, 1.0f, 0.95f);    // cyan unconfirmed
const glm::vec4 kConfirmedArrowColor(0.20f, 0.95f, 0.35f, 0.95f); // green main-chain tip
// Magenta/violet — lockstep completeness trace (stands out from cyan/green/gold).
const glm::vec4 kTraceArrowColor(0.92f, 0.28f, 0.95f, 0.98f);
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

// Key: listing block (owns deps[]) then dependency. Arrow draws listing → dep.
std::string tip_edge_key(const std::string& listing, const std::string& dep)
{
    return std::string("t|") + listing + '|' + dep;
}
std::string trace_edge_key(const std::string& listing, const std::string& dep)
{
    return std::string("tr|") + listing + '|' + dep;
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

    // Active tip listing → dep edges this frame (live max-height tips = cyan; frontier = green).
    std::unordered_set<std::string> active_keys;
    active_keys.reserve(64);

    struct ActiveEdge
    {
        std::string key;
        std::string listing; // block that owns deps[]
        std::string dep;
        glm::vec3 from_pos{}; // listing
        glm::vec3 to_pos{};   // dependency
        int stagger_i = 0;
        bool on_frontier = false; // sequential confirmed cursor tip
    };
    std::vector<ActiveEdge> active;
    active.reserve(64);

    // Frontier hashes (sequential H_c) — green arrows + Sobel.
    // Live max-height tips that are not on the frontier stay cyan.
    // IMPORTANT: frontier is often behind max-height tip; always include frontier listings.
    std::unordered_set<std::string> frontier_set;
    {
        const auto frontier = scene_.confirmed_frontier_ids_locked();
        frontier_set.insert(frontier.begin(), frontier.end());
    }

    std::vector<NodeId> listing_ids;
    listing_ids.reserve(32);
    {
        std::unordered_set<std::string> seen;
        for (const NodeId& id : scene_.confirmed_frontier_ids_locked())
        {
            if (seen.insert(id).second)
                listing_ids.push_back(id);
        }
        for (const NodeId& id : scene_.tip_ids())
        {
            if (seen.insert(id).second)
                listing_ids.push_back(id);
        }
    }

    int tip_stagger_base = 0;
    for (const NodeId& tip_hash : listing_ids)
    {
        auto d = scene_.detail_store().get(tip_hash);
        if (!d)
            continue;
        auto listing_it = positions.find(tip_hash);
        if (listing_it == positions.end())
            continue;

        const bool on_frontier = frontier_set.count(tip_hash) != 0;
        int stagger_i = 0;
        for (const std::string& dep_hash : d->deps)
        {
            auto dep_it = positions.find(dep_hash);
            if (dep_it == positions.end())
                continue;
            ActiveEdge e;
            e.key = tip_edge_key(tip_hash, dep_hash);
            e.listing = tip_hash;
            e.dep = dep_hash;
            // Arrow grows from listing block toward each dependency.
            e.from_pos = listing_it->second;
            e.to_pos = dep_it->second;
            e.stagger_i = tip_stagger_base + stagger_i++;
            e.on_frontier = on_frontier;
            active_keys.insert(e.key);
            active.push_back(std::move(e));
        }
        tip_stagger_base += 2; // slight global cascade across tips
    }

    // Lockstep completeness edges (adapter-published) — magenta, animated.
    struct TraceActive
    {
        std::string key;
        glm::vec3 from_pos{};
        glm::vec3 to_pos{};
        int stagger_i = 0;
    };
    std::vector<TraceActive> trace_active;
    std::unordered_set<std::string> trace_keys;
    {
        const auto tedges = scene_.trace_edges_locked();
        int ti = 0;
        for (const TraceEdge& te : tedges)
        {
            if (te.from.empty() || te.to.empty())
                continue;
            auto from_it = positions.find(te.from);
            auto to_it = positions.find(te.to);
            if (from_it == positions.end() || to_it == positions.end())
                continue;
            TraceActive ta;
            ta.key = trace_edge_key(te.from, te.to);
            ta.from_pos = from_it->second;
            ta.to_pos = to_it->second;
            ta.stagger_i = ti++;
            trace_keys.insert(ta.key);
            trace_active.push_back(std::move(ta));
        }
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

        // Green when listing tip is the sequential confirmed frontier (not mere bag membership).
        const bool conf = e.on_frontier;
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

    // Trace edges: separate anim map entries with tr| prefix; magenta color.
    for (const TraceActive& ta : trace_active)
    {
        DepArrowAnim& anim = tip_dep_anims_[ta.key];
        anim.from_pos = ta.from_pos;
        anim.to_pos = ta.to_pos;
        anim.has_pos = true;
        anim.base_alpha = kTraceArrowColor.a;
        anim.tip_scale = 1.15f; // slightly larger for standout
        anim.tip_confirmed = false;
        anim.confirm_blend_t = 0.f;
        if (anim.phase == ArrowPhase::Gone || anim.phase == ArrowPhase::Fading)
        {
            anim.phase = ArrowPhase::Held;
            anim.fade_start_sec = 0.f;
            if (anim.birth_sec < 0.f)
                anim.birth_sec = now;
            continue;
        }
        if (anim.birth_sec < 0.f)
        {
            anim.birth_sec = now + kArrowStagger * static_cast<float>(ta.stagger_i);
            anim.phase = ArrowPhase::Growing;
        }
        else if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age >= kArrowGrowSec)
                anim.phase = ArrowPhase::Held;
        }
    }

    // Edges no longer active tips: fade if nodes still live; erase if removed from graph.
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        const std::string& key = it->first;
        DepArrowAnim& anim = it->second;
        const bool is_trace = key.size() >= 3 && key[0] == 't' && key[1] == 'r' && key[2] == '|';

        // Parse listing|dep from key "t|listing|dep" or "tr|listing|dep"
        std::string listing, dep;
        {
            const size_t p0 = key.find('|');
            const size_t p1 = (p0 == std::string::npos) ? std::string::npos : key.find('|', p0 + 1);
            if (p0 != std::string::npos && p1 != std::string::npos)
            {
                listing = key.substr(p0 + 1, p1 - p0 - 1);
                dep = key.substr(p1 + 1);
            }
        }

        const bool live = !listing.empty() && !dep.empty() &&
                          live_nodes.count(listing) && live_nodes.count(dep);

        if (!live)
        {
            it = tip_dep_anims_.erase(it);
            continue;
        }

        const bool still_active = is_trace ? (trace_keys.count(key) != 0)
                                           : (active_keys.count(key) != 0);
        if (!still_active)
        {
            if (anim.phase == ArrowPhase::Growing || anim.phase == ArrowPhase::Held)
            {
                anim.phase = ArrowPhase::Fading;
                anim.fade_start_sec = now;
            }
            else if (anim.phase == ArrowPhase::Fading)
            {
                const float fade_age = now - anim.fade_start_sec;
                if (fade_age >= kArrowFadeSec)
                    anim.phase = ArrowPhase::Gone;
            }
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
        const bool is_trace =
            kv.first.size() >= 3 && kv.first[0] == 't' && kv.first[1] == 'r' && kv.first[2] == '|';

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

        glm::vec4 color;
        if (is_trace)
        {
            color = kTraceArrowColor;
            color.a = alpha;
        }
        else
        {
            color = glm::mix(kActiveArrowColor, kConfirmedArrowColor, anim.confirm_blend_t);
            color.a = alpha;
        }
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

    // --- Live pool classification (graph + detail), never frustum/draw set ---
    // solid[h]: sequenced from main tip AND all deps in pool AND same-chain deps solid
    //   (cascade so nothing above a broken / unsequenced link becomes opaque).
    std::unordered_map<std::string, bool> missing_dep;
    missing_dep.reserve(graph_nodes.size());
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
    }

    std::unordered_map<std::string, int> solid_memo; // 0 unknown, 1 solid, -1 not
    solid_memo.reserve(graph_nodes.size());
    std::function<bool(const std::string&)> is_solid;
    is_solid = [&](const std::string& h) -> bool {
        auto it = solid_memo.find(h);
        if (it != solid_memo.end())
            return it->second > 0;
        solid_memo[h] = -1; // cycle guard
        // Opaque only for successfully sequenced main-path blocks.
        if (!scene_.is_sequenced_locked(h))
            return false;
        if (missing_dep[h])
            return false;
        auto d = scene_.detail_store().get(h);
        auto self = scene_.graph().get(h);
        if (d && self)
        {
            for (const std::string& dep : d->deps)
            {
                if (dep.empty() || live_nodes.count(dep) == 0)
                    continue;
                auto dn = scene_.graph().get(dep);
                if (!dn)
                    continue;
                // Same-chain deps must also be solid (cascade stops at break).
                if (dn->lane != self->lane)
                    continue;
                if (!is_solid(dep))
                    return false;
            }
        }
        solid_memo[h] = 1;
        return true;
    };
    for (const GraphNode& n : graph_nodes)
        (void)is_solid(n.id);

    // Pool-first incomplete set (for force-draw + orange Sobel).
    std::vector<std::string> incomplete_pool;
    incomplete_pool.reserve(64);
    for (const GraphNode& n : graph_nodes)
    {
        if (!missing_dep[n.id])
            continue;
        incomplete_pool.push_back(n.id);
        if (incomplete_pool.size() >= 64)
            break;
    }

    out.instances.reserve(layout.placements.size());
    out.pick_map.reserve(layout.placements.size());
    constexpr float kUnconfirmedAlpha = 0.38f;
    const float scale = kDefaultBlockScale;

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

        // Opaque only when sequenced from tip and all deps known (cascade solid).
        const float alpha = is_solid(placed.hash) ? 1.0f : kUnconfirmedAlpha;
        out.instances.push_back(GpuInstance{ placed.pos, scale, color, alpha });
        out.pick_map.push_back(placed.hash);
        return true;
    };

    // Pass 1: frustum-culled draw (presentation only; does not redefine the pool).
    std::unordered_set<std::string> drawn;
    for (const PlacedBlock& placed : layout.placements)
    {
        if (push_instance(placed, /*force=*/false))
            drawn.insert(placed.hash);
    }

    // Pass 2: force-draw incomplete + confirmed so Sobel has instances.
    for (const PlacedBlock& placed : layout.placements)
    {
        if (drawn.count(placed.hash))
            continue;
        const bool force =
            missing_dep[placed.hash] || scene_.is_confirmed_locked(placed.hash);
        if (!force)
            continue;
        if (push_instance(placed, /*force=*/true))
            drawn.insert(placed.hash);
    }

    // Sobel lists: pool-first; cull only limits GPU indices.
    // Green = any confirmed main-chain block with an instance (not solid-gated).
    // Orange = incomplete (missing deps) that are not confirmed.
    {
        std::unordered_set<std::string> pick_set(out.pick_map.begin(), out.pick_map.end());

        // Prefer higher-height confirmed first when over cap.
        struct ConfItem { int height; std::string hash; };
        std::vector<ConfItem> conf_items;
        conf_items.reserve(64);
        for (const GraphNode& n : graph_nodes)
        {
            if (!scene_.is_confirmed_locked(n.id))
                continue;
            if (!pick_set.count(n.id))
                continue;
            conf_items.push_back({ static_cast<int>(n.height), n.id });
        }
        std::sort(conf_items.begin(), conf_items.end(),
                  [](const ConfItem& a, const ConfItem& b) { return a.height > b.height; });
        for (const auto& c : conf_items)
        {
            out.confirmed_tip_hashes.push_back(c.hash);
            if (out.confirmed_tip_hashes.size() >= 64)
                break;
        }

        for (const auto& h : incomplete_pool)
        {
            if (!pick_set.count(h))
                continue;
            // Confirmed blocks use green; orange is for incomplete non-confirmed.
            if (scene_.is_confirmed_locked(h))
                continue;
            out.incomplete_trace_hashes.push_back(h);
            if (out.incomplete_trace_hashes.size() >= 64)
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
            auto listing_it = block_positions.find(block.hash);
            if (listing_it == block_positions.end())
                return;
            const glm::vec3& listing_pos = listing_it->second;
            const int parent_lane = block.chain_idx();
            int missing_i = 0;
            int stagger_i = 0;

            for (const std::string& dep_hash : block.deps)
            {
                auto dep_it = block_positions.find(dep_hash);
                if (dep_it != block_positions.end())
                {
                    // Listing block → dependency.
                    add_eph_arrow('s', block.hash, dep_hash, listing_pos, dep_it->second,
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
                    listing_pos.z + meters_per_height);
                debug->add_wire_box(ghost, ghost_half, kMissingOutline);
                debug->add_line(listing_pos, ghost, kMissingOutline);
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
    scene_.get_trace_status_locked(&out.ui.trace_phase, &out.ui.trace_offset);
    {
        // HUD: live max-height tips vs sequential confirmed frontier + per-chain H_c.
        const auto tips = scene_.tip_ids();
        out.ui.tip_count = static_cast<int>(tips.size());
        out.ui.confirmed_tip_count =
            static_cast<int>(scene_.confirmed_frontier_ids_locked().size());
        scene_.copy_confirmed_heights_locked(out.ui.confirmed_height_by_lane);
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
