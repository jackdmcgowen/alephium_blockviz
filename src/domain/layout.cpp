#include "app/pch.h"
#include "domain/layout.hpp"
#include "domain/alph_block.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LanePalette LanePalette::default_alephium()
{
    // Brand-aligned 16-lane set (docs/brand/alephium_palette.md).
    // Ordered cool slate → brand orange → warm sand; no pure white/neon green
    // (reserved for semantic roles). 4 groups × 4 chains.
    LanePalette p;
    // Group 0 (chains 0→*) — slate / steel
    p.colors[0]  = glm::vec3(0.45f, 0.52f, 0.62f);
    p.colors[1]  = glm::vec3(0.55f, 0.60f, 0.72f);
    p.colors[2]  = glm::vec3(0.38f, 0.48f, 0.58f);
    p.colors[3]  = glm::vec3(0.62f, 0.68f, 0.78f);
    // Group 1 — teal / cyan-muted
    p.colors[4]  = glm::vec3(0.25f, 0.58f, 0.62f);
    p.colors[5]  = glm::vec3(0.32f, 0.68f, 0.70f);
    p.colors[6]  = glm::vec3(0.20f, 0.50f, 0.58f);
    p.colors[7]  = glm::vec3(0.40f, 0.72f, 0.68f);
    // Group 2 — brand orange family
    p.colors[8]  = glm::vec3(1.00f, 0.36f, 0.00f); // brand orange
    p.colors[9]  = glm::vec3(1.00f, 0.48f, 0.15f);
    p.colors[10] = glm::vec3(0.90f, 0.32f, 0.05f);
    p.colors[11] = glm::vec3(1.00f, 0.55f, 0.28f);
    // Group 3 — warm sand / amber (distinct from tip green / select gold)
    p.colors[12] = glm::vec3(0.78f, 0.62f, 0.38f);
    p.colors[13] = glm::vec3(0.85f, 0.70f, 0.45f);
    p.colors[14] = glm::vec3(0.70f, 0.55f, 0.32f);
    p.colors[15] = glm::vec3(0.92f, 0.78f, 0.52f);
    return p;
}

glm::vec3 LanePalette::color_for(uint32_t lane) const
{
    return colors[lane % 16];
}

PolarShardLayout::PolarShardLayout(LanePalette palette)
    : palette_(std::move(palette))
{
}

LayoutResult PolarShardLayout::build(const std::vector<GraphNode>& nodes, const LayoutParams& params)
{
    LayoutResult out;
    out.placements.reserve(nodes.size());
    out.positions.reserve(nodes.size());
    out.lanes.reserve(nodes.size());

    const uint32_t lane_count = params.lane_count > 0 ? params.lane_count : 16;
    const float mps = params.meters_per_second > 1e-6f ? params.meters_per_second : 1.f;
    const int64_t origin_ms = params.timeline_origin_ms;

    // lane -> timestamp_ms -> nodes (stable order by id for same-ms forks)
    std::map<uint32_t, std::map<int64_t, std::vector<const GraphNode*>>> by_lane_ts;
    for (const GraphNode& n : nodes)
    {
        if (n.id.empty() || n.lane >= lane_count)
            continue;
        int64_t ts = n.timestamp_ms;
        if (ts <= 0 && n.height >= 0)
        {
            // Fallback: rough estimate so missing ts still places on the axis.
            ts = origin_ms + static_cast<int64_t>(n.height) *
                                 static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;
        }
        by_lane_ts[n.lane][ts].push_back(&n);
    }

    for (auto& lane_entry : by_lane_ts)
    {
        const uint32_t lane = lane_entry.first;
        for (auto& ts_entry : lane_entry.second)
        {
            auto& at_ts = ts_entry.second;
            std::sort(at_ts.begin(), at_ts.end(),
                      [](const GraphNode* a, const GraphNode* b) { return a->id < b->id; });

            int block_index = 0;
            for (const GraphNode* np : at_ts)
            {
                const GraphNode& node = *np;
                const int shardId = static_cast<int>(node.lane);
                if (shardId < 0 || shardId >= 16)
                {
                    ++block_index;
                    continue;
                }

                int64_t ts = node.timestamp_ms > 0 ? node.timestamp_ms : ts_entry.first;
                const float seconds =
                    static_cast<float>(ts - origin_ms) * 0.001f;
                // Newer blocks toward more-negative Z (camera starts at -lookback seconds).
                const float z = -seconds * mps;

                const float angle =
                    (static_cast<float>(shardId) / static_cast<float>(lane_count)) *
                    2.0f * static_cast<float>(M_PI);
                // Same-timestamp forks: small radial offset (not height-based).
                const float radius =
                    params.base_radius + static_cast<float>(block_index) * 2.0f;

                PlacedBlock placed;
                placed.hash = node.id;
                placed.lane = static_cast<uint8_t>(shardId);
                placed.height = static_cast<int>(node.height);
                placed.timestamp_ms = ts;
                placed.txn_count = node.txn_count;
                placed.alph_out_atto = node.alph_out_atto;
                placed.is_uncle = (node.role == BlockRole::Uncle);
                // Slight extra radial ring for uncles so they sit outside main cubes.
                const float r = placed.is_uncle ? (radius + 3.0f) : radius;
                // Camera up=(0,-1,0) + forward +Z ⇒ world right ≈ −X, so +cos places
                // lane 0 (θ=0) on screen-left. Negate X so 0→0 sits on the viewer's right.
                placed.pos = glm::vec3(-r * std::cos(angle), r * std::sin(angle), z);
                placed.color = palette_.color_for(static_cast<uint32_t>(shardId));
                if (placed.is_uncle)
                {
                    // Brand palette uncle_violet.
                    const glm::vec3 uncle_tint(0.72f, 0.28f, 0.95f);
                    placed.color = glm::mix(placed.color, uncle_tint, 0.72f);
                }

                out.positions[placed.hash] = placed.pos;
                out.lanes[placed.hash] = placed.lane;
                out.placements.push_back(std::move(placed));
                ++block_index;
            }
        }
    }

    return out;
}
