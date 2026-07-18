#pragma once

// Polar layout for multi-lane block cubes. No Vulkan / cJSON.
// Z axis is a shared wall-clock timeline (block timestamps), not height index.
// Heights may diverge across shards — that is expected for a DAG.
#include "domain/block_graph.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct LayoutParams
{
    // World units per second of block timestamp (≈1 keeps ~8 m per 8 s block period).
    float    meters_per_second  = 1.0f;
    float    base_radius        = 20.0f;
    uint32_t lane_count         = 16;
    // Shared timeline origin (ms). Presenter sets once per session / frame anchor.
    int64_t  timeline_origin_ms = 0;
};

struct LanePalette
{
    glm::vec3 colors[16]{};

    static LanePalette default_alephium();
    glm::vec3 color_for(uint32_t lane) const;
};

struct PlacedBlock
{
    std::string hash;
    uint8_t     lane = 0;
    int         height = 0;
    int64_t     timestamp_ms = 0;
    glm::vec3   pos{ 0.0f };
    glm::vec3   color{ 1.0f };
};

struct LayoutResult
{
    std::vector<PlacedBlock> placements;
    std::unordered_map<std::string, glm::vec3> positions;
    std::unordered_map<std::string, uint8_t>   lanes;
};

// Polar XY by lane; Z from timestamp relative to timeline_origin_ms.
class PolarShardLayout
{
public:
    explicit PolarShardLayout(LanePalette palette = LanePalette::default_alephium());

    LayoutResult build(const std::vector<GraphNode>& nodes, const LayoutParams& params);

private:
    LanePalette palette_;
};
