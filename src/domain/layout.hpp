#pragma once

// Polar layout for multi-lane block cubes. No Vulkan / cJSON.
#include "alph_block.hpp"
#include "domain/block_graph.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

struct LayoutParams
{
    float meters_per_height = 8.0f;
    float base_radius       = 20.0f;
    uint32_t lane_count     = 16;
};

struct LanePalette
{
    // 16 shard colors (matches historical SHARD_COLORS)
    glm::vec3 colors[16]{};

    static LanePalette default_alephium();
    glm::vec3 color_for(uint32_t lane) const;
};

struct PlacedBlock
{
    std::string hash;
    uint8_t     lane = 0;
    int         height = 0;
    glm::vec3   pos{ 0.0f };
    glm::vec3   color{ 1.0f };
    const AlphBlock* block = nullptr; // non-owning; valid only during caller's lock
};

struct LayoutResult
{
    std::vector<PlacedBlock> placements;
    std::unordered_map<std::string, glm::vec3> positions;
    std::unordered_map<std::string, uint8_t>   lanes;
};

// Session-local polar layout (origin height per lane like previous start_height[]).
class PolarShardLayout
{
public:
    explicit PolarShardLayout(LanePalette palette = LanePalette::default_alephium());

    // chains[lane] = height -> hash -> AlphBlock (same structure as BlockScene)
    using HashToBlocks = std::map<std::string, AlphBlock>;
    using HeightToHash = std::map<uint64_t, HashToBlocks>;

    LayoutResult build(const std::vector<HeightToHash>& chains, const LayoutParams& params);

    void reset_origins();

private:
    LanePalette palette_;
    int origin_height_by_lane_[16]{};
    bool origin_set_[16]{};
};
