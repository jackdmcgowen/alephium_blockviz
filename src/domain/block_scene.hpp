#pragma once

// Domain scene model: BlockGraph + AlphDetailStore + recent feed.
// Adapter writes; renderer reads under mutex.
//
// Block state model (BlockFlow viz) — production keep list:
//   confirmed_          — proven main-chain bag (solid when deps live)
//   confirmed_hash_[L]  — sequential frontier tip H_c (green Sobel + full blockDeps arrows)
//   frontier_walk_[L]   — multi-step green walk animation path (chain-walk tip jump)
//   bfs_traces_         — parallel BFS confirm rays (N=2G-1), short edge paths
//   pending_hash_[L]    — adapter next-seed bookkeeping only (not a render input;
//                         cyan frontier-children are classified in ScenePresenter)
#include "domain/alph_block.hpp"
#include "domain/block_graph.hpp"
#include "network/alephium/alph_detail_store.hpp"

#include <atomic>
#include <cjson/cJSON.h>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

struct RecentFeedItem
{
    NodeId hash;
    int    chainFrom = 0;
    int    chainTo   = 0;
    int    height    = 0;
};

class BlockScene
{
public:
    static constexpr int kLaneCount = 16;

    BlockScene();

    // Full clear for domain switch (graph, details, feed, frontiers, walks).
    void reset();

    bool add_block(cJSON* block);
    // Synthetic / FakeChain path (no cJSON). Same semantics as JSON add.
    bool add_block(const AlphBlock& alph_block);
    bool remove_block(const std::string& hash);

    // Retention: drop old/excess nodes from graph, detail store, confirmed bag, feed.
    // Keeps per-lane confirmed frontier tips and pending tips. Returns removed count.
    // min_timestamp_ms <= 0 skips time filter; max_nodes == 0 skips count cap.
    size_t prune(int64_t min_timestamp_ms, size_t max_nodes);

    // Sequential frontier: H_c+1 default; chain_walk allows validated multi-step jump.
    // Bag membership always records proven main for solid drawing.
    void mark_confirmed(const NodeId& hash);
    void mark_confirmed(const NodeId& hash, uint32_t lane, int height, bool chain_walk = false);

    // Green-tip walk path (old frontier → new tip); presenter animates.
    void set_frontier_walk(uint32_t lane, std::vector<NodeId> path_old_to_new);
    // Presenter only: call while holding scene.mutex().
    std::vector<NodeId> frontier_walk_locked(uint32_t lane) const;
    void clear_frontier_walk_locked(uint32_t lane);

    // Parallel BFS confirm traces (adapter writes; presenter draws short paths).
    // N = 2*ALPH_NUM_GROUPS - 1; edges are parent→dep (newer→older).
    static constexpr int kBfsThreadCount = 2 * ALPH_NUM_GROUPS - 1;
    static constexpr int kBfsTraceMaxEdges = 48;
    struct BfsTraceSnap
    {
        int  thread_id  = -1;
        int  generation = 0;
        int  active     = 0; // 0 idle, 1 running, 2 paused
        NodeId head;
        // Parallel arrays, size <= kBfsTraceMaxEdges.
        std::vector<NodeId> edge_from;
        std::vector<NodeId> edge_to;
    };
    void set_bfs_traces(const BfsTraceSnap* traces, int n);
    // Presenter only: hold mutex().
    void copy_bfs_traces_locked(BfsTraceSnap out[kBfsThreadCount], int* n_out) const;

    // Adapter next-seed bookkeeping (not cyan Sobel). Self-locking.
    void set_pending_tip(uint32_t lane, const NodeId& hash);
    void clear_pending_tip(uint32_t lane);
    NodeId pending_tip_hash(uint32_t lane) const;

    int    confirmed_height(uint32_t lane) const;
    NodeId confirmed_tip_hash(uint32_t lane) const;
    bool   frontier_valid(uint32_t lane) const;

    std::mutex& mutex() { return mu_; }
    const std::mutex& mutex() const { return mu_; }

    AlphDetailStore& detail_store() { return detail_store_; }
    const AlphDetailStore& detail_store() const { return detail_store_; }
    BlockGraph& graph() { return graph_; }
    const BlockGraph& graph() const { return graph_; }
    const std::deque<RecentFeedItem>& feed() const { return feed_; }
    int total_blocks() const { return total_blocks_; }

    std::vector<GraphNode> nodes_snapshot() const { return graph_.nodes_snapshot(); }
    std::vector<NodeId> tip_ids() const;
    size_t unconfirmed_live_count() const;

    void  set_camera_scroll_z(float z) { camera_scroll_z_.store(z, std::memory_order_relaxed); }
    float camera_scroll_z() const { return camera_scroll_z_.load(std::memory_order_relaxed); }

    // Genesis / chain-start timestamp (ms); adapter resolves, camera uses for Z limits.
    void    set_genesis_ms(int64_t ms) { genesis_ms_.store(ms, std::memory_order_relaxed); }
    int64_t genesis_ms() const { return genesis_ms_.load(std::memory_order_relaxed); }

    // Layout timeline origin (ms); presenter/render may publish for camera follow.
    void    set_timeline_origin_ms(int64_t ms)
    {
        timeline_origin_ms_.store(ms, std::memory_order_relaxed);
    }
    int64_t timeline_origin_ms() const
    {
        return timeline_origin_ms_.load(std::memory_order_relaxed);
    }

    bool is_confirmed_locked(const NodeId& hash) const;
    std::vector<NodeId> confirmed_frontier_ids_locked() const;
    std::vector<NodeId> pending_tip_ids_locked() const;
    int confirmed_height_locked(uint32_t lane) const;
    void copy_confirmed_heights_locked(int out[kLaneCount]) const;

    // Ghost uncle / not-main competitor kept for visualization (not confirm tip).
    void mark_uncle(const NodeId& hash);
    bool is_uncle(const NodeId& hash) const;
    bool is_uncle_locked(const NodeId& hash) const;

    void set_trace_status(int phase, int offset);
    void get_trace_status_locked(int* phase_out, int* offset_out) const;

    // Timeline segment = lookback window [from_ms, to_ms).
    // index = lookback k (0 = live); global_index = id from chain genesis → live.
    static constexpr int kMaxTimeSegments = 32;
    struct TimeSegment
    {
        int     index            = 0;  // lookback k
        int     global_index     = -1; // G from genesis
        int64_t from_ms          = 0;
        int64_t to_ms            = 0; // exclusive
        float   load_ratio       = 0.f;
        int     confirmed_full   = 0; // 0/1
        int     block_count      = 0;
        int     expected_blocks  = 0;
    };

    // Network HUD published by adapter (copied into UiSnapshot on prepare).
    struct NetworkHud
    {
        int         domain               = 0; // NetworkDomain
        int         status               = 0; // NetworkStatus
        char        base_url[160]        = {};
        int         lookback_windows_done = 0;
        int         lookback_windows_need = 1;
        int         lanes_with_frontier  = 0;
        int         open_confirm_walks   = 0;
        int         tip_height_by_lane[kLaneCount]{};
        int         stats_api_is_main    = 0;
        int         stats_fetch_admitted = 0;
        int         stats_removed        = 0;
        int         stats_seed_q         = 0;
        int64_t     last_poll_ms         = 0;
        float       poll_interval_sec    = 8.f;
        int         switching            = 0;
        int         segment_count        = 0;
        TimeSegment segments[kMaxTimeSegments]{};
        // Timeline graph cache pressure: 0 ok, 1 soft warn, 2 hard (may drop oldest).
        int         cache_pressure_level = 0;
        // 0 = Live (k0 in sliding window), 1 = History (live tip halted).
        int         browse_mode = 0;
    };
    void set_network_hud(const NetworkHud& hud);
    NetworkHud network_hud() const;
    // Caller must already hold mutex() (e.g. ScenePresenter::prepare).
    NetworkHud network_hud_locked() const;

    AlphBlock resolve_detail(const std::string& hash) const;

private:
    void mark_confirmed_unlocked_(const NodeId& hash);
    void mark_confirmed_unlocked_(const NodeId& hash, uint32_t lane, int height,
                                  bool chain_walk = false);
    void erase_confirmed_unlocked_(const NodeId& hash);
    bool is_confirmed_unlocked_(const NodeId& hash) const;
    void refresh_frontier_lane_unlocked_(uint32_t lane);

    mutable std::mutex mu_;
    BlockGraph graph_;
    AlphDetailStore detail_store_;
    std::deque<RecentFeedItem> feed_;
    int total_blocks_ = 0;
    std::unordered_set<NodeId> confirmed_;
    std::unordered_set<NodeId> uncle_set_;
    std::atomic<float> camera_scroll_z_{ 0.f };
    std::atomic<int64_t> genesis_ms_{ ALPH_GENESIS_TIMESTAMP_MS_FALLBACK };
    std::atomic<int64_t> timeline_origin_ms_{ 0 };

    int    confirmed_height_[kLaneCount]{};
    NodeId confirmed_hash_[kLaneCount]{};
    bool   frontier_valid_[kLaneCount]{};
    NodeId pending_hash_[kLaneCount]{};
    std::vector<NodeId> frontier_walk_[kLaneCount]{};
    BfsTraceSnap bfs_traces_[kBfsThreadCount]{};
    int          bfs_trace_n_ = 0;

    int trace_phase_  = 0;
    int trace_offset_ = 0;

    NetworkHud network_hud_{};
};
