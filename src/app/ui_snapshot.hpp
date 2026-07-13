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
    // tip_count = live max-height tips; confirmed_tip_count = sequential confirmed frontier (H_c).
    int                   tip_count            = 0;
    int                   confirmed_tip_count  = 0;
    // Highest sequential confirmed height per lane (chainFrom*4+chainTo). -1 = not initialized.
    int                   confirmed_height_by_lane[16]{};
    // Adapter confirm phase (0 Bootstrap, 1 IdentifyTips, 2 Lockstep, 3 Steady).
    int                   trace_phase  = 0;
    int                   trace_offset = 0;
    uint64_t              seq                  = 0;

    UiSnapshot()
    {
        for (int& h : confirmed_height_by_lane)
            h = -1;
    }
};
