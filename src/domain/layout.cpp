#include "domain/layout.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LanePalette LanePalette::default_alephium()
{
    LanePalette p;
    p.colors[0]  = glm::vec3(1.00f, 0.34f, 0.20f); // Orange
    p.colors[1]  = glm::vec3(0.20f, 1.00f, 0.34f); // Green
    p.colors[2]  = glm::vec3(0.20f, 0.34f, 1.00f); // Blue
    p.colors[3]  = glm::vec3(1.00f, 0.20f, 1.00f); // Pink
    p.colors[4]  = glm::vec3(1.00f, 0.76f, 0.00f); // Yellow
    p.colors[5]  = glm::vec3(0.85f, 0.97f, 0.65f); // Light Green
    p.colors[6]  = glm::vec3(0.78f, 0.00f, 0.22f); // Dark Red
    p.colors[7]  = glm::vec3(0.34f, 0.09f, 0.27f); // Dark Purple
    p.colors[8]  = glm::vec3(1.00f, 1.00f, 1.00f); // White
    p.colors[9]  = glm::vec3(0.50f, 0.50f, 0.00f); // Olive
    p.colors[10] = glm::vec3(0.00f, 1.00f, 1.00f); // Aqua
    p.colors[11] = glm::vec3(1.00f, 0.75f, 0.80f); // Pink
    p.colors[12] = glm::vec3(0.50f, 0.00f, 0.50f); // Purple
    p.colors[13] = glm::vec3(1.00f, 1.00f, 0.00f); // Yellow
    p.colors[14] = glm::vec3(0.50f, 0.50f, 0.50f); // Grey
    p.colors[15] = glm::vec3(0.00f, 1.00f, 0.00f); // Lime
    return p;
}

glm::vec3 LanePalette::color_for(uint32_t lane) const
{
    return colors[lane % 16];
}

PolarShardLayout::PolarShardLayout(LanePalette palette)
    : palette_(std::move(palette))
{
    reset_origins();
}

void PolarShardLayout::reset_origins()
{
    for (int i = 0; i < 16; ++i)
    {
        origin_height_by_lane_[i] = 0;
        origin_set_[i] = false;
    }
}

LayoutResult PolarShardLayout::build(const std::vector<GraphNode>& nodes, const LayoutParams& params)
{
    LayoutResult out;
    out.placements.reserve(nodes.size());
    out.positions.reserve(nodes.size());
    out.lanes.reserve(nodes.size());

    const uint32_t lane_count = params.lane_count > 0 ? params.lane_count : 16;

    // lane -> height -> nodes at that height (stable order by id for forks)
    std::map<uint32_t, std::map<int64_t, std::vector<const GraphNode*>>> by_lane_height;
    for (const GraphNode& n : nodes)
    {
        if (n.id.empty() || n.lane >= lane_count)
            continue;
        by_lane_height[n.lane][n.height].push_back(&n);
    }

    for (auto& lane_entry : by_lane_height)
    {
        const uint32_t lane = lane_entry.first;
        for (auto& height_entry : lane_entry.second)
        {
            auto& at_height = height_entry.second;
            std::sort(at_height.begin(), at_height.end(),
                      [](const GraphNode* a, const GraphNode* b) { return a->id < b->id; });

            int block_index = 0;
            for (const GraphNode* np : at_height)
            {
                const GraphNode& node = *np;
                const int shardId = static_cast<int>(node.lane);
                if (shardId < 0 || shardId >= 16)
                {
                    ++block_index;
                    continue;
                }

                if (!origin_set_[shardId])
                {
                    origin_height_by_lane_[shardId] = static_cast<int>(node.height);
                    origin_set_[shardId] = true;
                }

                const float angle =
                    (static_cast<float>(shardId) / static_cast<float>(lane_count)) *
                    2.0f * static_cast<float>(M_PI);
                const float radius = params.base_radius + block_index * params.meters_per_height;
                const float z =
                    -static_cast<float>(static_cast<int>(node.height) - origin_height_by_lane_[shardId]) *
                    params.meters_per_height;

                PlacedBlock placed;
                placed.hash = node.id;
                placed.lane = static_cast<uint8_t>(shardId);
                placed.height = static_cast<int>(node.height);
                placed.pos = glm::vec3(radius * std::cos(angle), radius * std::sin(angle), z);
                placed.color = palette_.color_for(static_cast<uint32_t>(shardId));

                out.positions[placed.hash] = placed.pos;
                out.lanes[placed.hash] = placed.lane;
                out.placements.push_back(std::move(placed));
                ++block_index;
            }
        }
    }

    return out;
}
