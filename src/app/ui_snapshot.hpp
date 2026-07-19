#pragma once

// Published UI view for render-thread ImGui (PR7).
// Overlay must not read live BlockScene / adapter state without going through this.
#include "domain/alph_block.hpp"
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
    // Adapter confirm phase (0 Bootstrap, 1 Identify tips, 2 Confirm walk, 3 Steady).
    int                   trace_phase  = 0;
    int                   trace_offset = 0; // lanes still walking confirm path
    uint64_t              seq                  = 0;

    // --- Network left rail ---
    int         net_domain = 0;          // NetworkDomain
    int         net_status = 0;          // NetworkStatus
    char        net_base_url[160]{};
    int         lookback_windows_done = 0;
    int         lookback_windows_need = 1;
    int         lanes_with_frontier   = 0;
    int         open_confirm_walks    = 0;
    int         tip_height_by_lane[16]{};
    int         stats_api_is_main     = 0;
    int         stats_fetch_admitted  = 0;
    int         stats_removed         = 0;
    int         stats_seed_q          = 0;
    int64_t     last_poll_ms          = 0;
    float       poll_interval_sec     = 8.f;
    int         net_switching         = 0;
    // Timeline graph cache: 0 ok, 1 soft warn, 2 hard (may drop oldest blocks).
    int         cache_pressure_level  = 0;
    // 0 = Live mode, 1 = History mode (live tip build halted).
    int         browse_mode           = 0;
    // Segment disk cache HUD.
    int         disk_cache_segments   = 0;
    int         disk_cache_mb         = 0;
    int         disk_cache_boot_blocks = 0;

    // Timeline segments (mirror BlockScene::TimeSegment).
    static constexpr int kMaxTimeSegments = 32;
    struct TimeSegmentUi
    {
        int     index           = 0;  // lookback k (0 = live)
        int     global_index    = -1; // G from chain genesis → live
        int64_t from_ms         = 0;
        int64_t to_ms           = 0;
        float   load_ratio      = 0.f;
        int     confirmed_full  = 0;
        int     block_count     = 0;
        int     expected_blocks = 0;
    };
    int           segment_count = 0;
    TimeSegmentUi segments[kMaxTimeSegments]{};
    // Layout Z origin for minimap / camera jump (matches ScenePresenter).
    int64_t timeline_origin_ms = 0;
    // Chain genesis for segment numbers / minimap bounds (matches adapter/presenter).
    int64_t genesis_ms         = 0;
    float   meters_per_second  = 1.f;

    // World-anchored hover billboard (ImGui projects world_pos each frame).
    // Overlay owns fade alpha; presenter sets want_visible + content.
    struct BlockBillboardUi
    {
        bool  want_visible = false;
        char  hash[72]{};
        float world_pos[3]{ 0.f, 0.f, 0.f }; // slightly in front of cube toward camera
        int   height     = -1;
        int   chain_from = -1;
        int   chain_to   = -1;
        int   txn_count  = -1; // -1 = unknown (never parsed); survives detail slim
        int   is_uncle   = 0;  // 0/1 ghost uncle
        char  alph_out[48]{};  // human ALPH total of outputs; empty if unknown
    };
    BlockBillboardUi block_billboard{};

    UiSnapshot()
    {
        for (int& h : confirmed_height_by_lane)
            h = -1;
        for (int& h : tip_height_by_lane)
            h = -1;
    }
};
