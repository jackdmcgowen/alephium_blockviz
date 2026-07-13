#pragma once

// Published UI view for render-thread ImGui (PR7).
// Overlay must not read live BlockScene / adapter state without going through this.
#include "alph_block.hpp"
#include "domain/block_graph.hpp"

#include <cstdint>
#include <deque>
#include <string>

// Compact feed row (hash + shard for coloring); not full txn payloads.
struct FeedEntry
{
    NodeId  hash;
    int     chainFrom = 0;
    int     chainTo   = 0;
    int     height    = 0;

    uint8_t chain_idx() const
    {
        return static_cast<uint8_t>(chainFrom * ALPH_NUM_GROUPS + chainTo);
    }
};

struct UiSnapshot
{
    std::deque<FeedEntry> feed;
    NodeId                selected_hash;
    AlphBlock             selected_detail; // full block for inspector (txns)
    int                   total_blocks = 0;
    // Live tip frontier (max-height per lane); confirmed = main-chain tips among them.
    int                   tip_count            = 0;
    int                   confirmed_tip_count  = 0;
    uint64_t              seq                  = 0;
};
