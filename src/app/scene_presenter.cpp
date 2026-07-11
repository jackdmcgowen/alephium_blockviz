#include "app/scene_presenter.hpp"

#include "alph_block.hpp"
#include "graphics/debug/debug_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/glm.hpp>

namespace
{
// Layout spacing (mirrors previous engine static)
float meters_per_height = static_cast<float>(ALPH_TARGET_BLOCK_SECONDS);

const glm::vec4 kActiveArrowColor(0.15f, 0.95f, 1.0f, 0.95f);
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
} // namespace

ScenePresenter::ScenePresenter(BlockScene& scene)
    : scene_(scene)
{
}

float ScenePresenter::arrow_grow_u_(const std::string& key, float stagger_delay,
                                    std::unordered_set<std::string>& seen_this_frame)
{
    seen_this_frame.insert(key);
    const float now = std::chrono::duration<float>(
                          std::chrono::steady_clock::now() - clock0_)
                          .count();
    auto it = arrow_birth_sec_.find(key);
    if (it == arrow_birth_sec_.end())
    {
        arrow_birth_sec_[key] = now + stagger_delay;
        it = arrow_birth_sec_.find(key);
    }
    const float age = now - it->second;
    if (age <= 0.f)
        return 0.f;
    return std::clamp(age / kArrowGrowSec, 0.f, 1.f);
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
        // Mesh verts at ±1 → half-extents = scale (inflate slightly for cull).
        const glm::vec3 half = in.instance_half_extents * scale;

        // Frustum cull before GPU submit (pick_map stays index-aligned with instances).
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

    if (debug)
    {
        const float tip_len = std::max(0.18f, meters_per_height * 0.08f);
        const float tip_rad = std::max(0.06f, meters_per_height * 0.03f);
        const float shaft_r = tip_rad * 0.4f;
        const float clearance = std::max(0.55f, meters_per_height * 0.12f);
        constexpr uint32_t kDepRadial = 8;
        constexpr uint32_t kMaxDepArrows = 512;
        uint32_t arrow_count = 0;
        std::unordered_set<std::string> arrows_seen;

        auto arrow_key = [](char kind, const std::string& from, const std::string& to) {
            return std::string(1, kind) + '|' + from + '|' + to;
        };

        auto add_dep_arrow = [&](char kind, const std::string& from_hash,
                                 const std::string& to_hash,
                                 const glm::vec3& from, const glm::vec3& to,
                                 const glm::vec4& color, float tip_scale, int stagger_i) {
            if (arrow_count >= kMaxDepArrows)
                return;
            glm::vec3 from_inset, to_inset;
            if (!inset_segment(from, to, clearance, from_inset, to_inset))
                return;
            const std::string key = arrow_key(kind, from_hash, to_hash);
            const float grow = arrow_grow_u_(key, kArrowStagger * static_cast<float>(stagger_i),
                                             arrows_seen);
            if (grow <= 0.f)
                return;
            debug->add_arrow(from_inset, to_inset, color,
                             tip_len * tip_scale, tip_rad * tip_scale,
                             shaft_r * tip_scale, kDepRadial, grow);
            ++arrow_count;
        };

        auto draw_deps_of = [&](char kind, const AlphBlock& block, const glm::vec4& color,
                                float tip_scale) {
            auto to_it = block_positions.find(block.hash);
            if (to_it == block_positions.end())
                return;
            int stagger_i = 0;
            for (const std::string& dep_hash : block.deps)
            {
                if (arrow_count >= kMaxDepArrows)
                    break;
                auto from_it = block_positions.find(dep_hash);
                if (from_it == block_positions.end())
                    continue;
                add_dep_arrow(kind, dep_hash, block.hash, from_it->second, to_it->second,
                              color, tip_scale, stagger_i++);
            }
        };

        for (const NodeId& tip_hash : scene_.tip_ids())
        {
            if (auto d = scene_.detail_store().get(tip_hash))
                draw_deps_of('t', *d, kActiveArrowColor, 1.f);
        }

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
                    add_dep_arrow('s', dep_hash, block.hash, from_it->second, to_pos,
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
                draw_deps_of('h', *d, kHoverArrowColor, 1.05f);
        }

        // Drop birth times for arrows no longer drawn (re-grow when they return).
        for (auto it = arrow_birth_sec_.begin(); it != arrow_birth_sec_.end(); )
        {
            if (arrows_seen.find(it->first) == arrows_seen.end())
                it = arrow_birth_sec_.erase(it);
            else
                ++it;
        }
    }

    out.ui.total_blocks = scene_.total_blocks();
    out.ui.selected_hash = in.selected_hash;
    out.ui.selected_detail = in.selected_detail;
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
