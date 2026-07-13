#pragma once

// Domain/network policy for Alephium BlockFlow ingest.
// Bootstrap: one lookback poll → identify tips → pool-only per-chain DFS
// (terminate at first unknown dep, no blockhash fetch) → Steady.
// Older history: large window poll only when camera unlocks lookback periods;
// DFS resumes from per-lane stop points.
#include "adapters/alephium/block_fetch_pool.hpp"
#include "adapters/alephium/main_chain_cache.hpp"
#include "domain/block_scene.hpp"
#include "engine/blockviz_engine_api.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

class AlephiumAdapter
{
public:
    // Bootstrap → identify tips → independent per-chain DFS → free polling.
    enum class Phase : int
    {
        BootstrapPoll = 0, // allow first lookback poll only
        IdentifyTips  = 1, // is_main all live tips; poll gated
        DfsTrace      = 2, // per-lane DFS (not lockstep); poll gated
        Steady        = 3, // normal interval polls
    };

    struct Config
    {
        int64_t lookback_ms = 0;
        int64_t poll_interval_ms = 8000;
    };

    AlephiumAdapter(BlockScene& scene, IBlockvizEngine& engine);

    void configure(const Config& cfg);
    void reset_stats();

    void set_fetch_pool(BlockFetchPool* pool) { fetch_pool_ = pool; }

    void on_start();

    void poll_once(int64_t& last_poll_ts);

    // False during IdentifyTips / DfsTrace (and before first poll done).
    bool ready_for_poll() const;

    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    void maybe_refill_selection_detail();

    Phase phase() const { return phase_; }
    // HUD: number of lanes still running DFS (0 when idle).
    int   trace_offset() const;

    size_t verify_queue_size() const { return seed_q_.size(); }
    int stats_verified_ok() const { return stats_verified_ok_; }
    int stats_removed() const { return stats_removed_; }
    int stats_replaced() const { return stats_replaced_; }
    int stats_detail_refilled() const { return stats_detail_refilled_; }
    int stats_confirmed_marks() const { return stats_confirmed_marks_; }
    int stats_dag_floods() const { return stats_dag_floods_; }
    int stats_api_is_main() const { return stats_api_is_main_; }
    int stats_uncles_checked() const { return stats_uncles_checked_; }
    int stats_uncles_removed() const { return stats_uncles_removed_; }
    int stats_fetch_admitted() const { return stats_fetch_admitted_; }
    int stats_trace_missing() const { return stats_trace_missing_; }

    int64_t poll_interval_ms() const { return cfg_.poll_interval_ms; }
    int64_t lookback_ms() const { return cfg_.lookback_ms; }

private:
    struct SeedJob
    {
        std::string hash;
        int from = 0;
        int to = 0;
        int height = 0;
    };

    void enqueue_seed_(SeedJob job);
    bool pop_seed_round_robin_(SeedJob& out);
    void confirm_seed_(const SeedJob& seed);
    // Same-chain offline mark from tip; does not walk past missing deps.
    int flood_confirm_deps_offline_(const std::string& main_hash, int budget);
    int maybe_flood_offline_(const std::string& main_hash, uint32_t lane, int height,
                             bool force);
    bool lane_needs_reflood_(uint32_t lane) const;
    bool fetch_and_admit_(const std::string& hash);
    void replace_non_main_(const SeedJob& job);
    void verify_uncle_(const std::string& uncle_hash, int parent_from, int parent_to,
                       int parent_height);
    void enqueue_uncles_from_block_(const AlphBlock& alph);
    void label_tips_needing_reflood_();
    void refresh_lookback_floors_();
    bool height_in_lookback_(uint32_t lane, int height) const;
    int  effective_lookback_floor_(uint32_t lane) const;
    int  camera_extra_lookback_heights_() const;
    bool tip_pending_confirmation_() const;

    void drain_fetch_results_(int max_admits);
    bool enqueue_missing_dep_(const std::string& dep_hash); // selection/legacy only

    // Phase / per-chain pool-only DFS
    void set_phase_(Phase p);
    void maybe_enter_dfs_();
    void advance_dfs_traces_();
    // Pool-only walk; terminate at first missing dep (no fetch). Always finishes.
    void run_dfs_lane_(uint32_t lane, std::vector<TraceEdge>& edges_out, bool from_stop);
    bool all_dfs_done_() const;
    void maybe_camera_history_extend_();
    int  admit_blocks_with_events_(cJSON* obj, int* seen_out, int* added_out);
    void publish_trace_status_();

    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);
    void prune_detail_store();

    BlockScene& scene_;
    IBlockvizEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;
    BlockFetchPool* fetch_pool_ = nullptr;

    std::deque<SeedJob> seed_q_;
    std::unordered_set<std::string> seed_queued_;
    std::unordered_set<std::string> proven_not_main_;
    std::deque<std::string> broken_dep_q_;
    std::unordered_set<std::string> broken_dep_seen_;
    std::unordered_set<std::string> broken_dep_failed_;
    std::deque<SeedJob> uncle_q_;
    std::unordered_set<std::string> uncle_queued_;

    Phase phase_ = Phase::BootstrapPoll;
    bool bootstrap_poll_done_ = false;
    // Per-lane DFS: inactive or finished for current unlocked floor.
    bool   dfs_active_[BlockScene::kLaneCount]{};
    bool   dfs_done_[BlockScene::kLaneCount]{};
    NodeId dfs_stop_hash_[BlockScene::kLaneCount]{};
    int    dfs_stop_height_[BlockScene::kLaneCount]{};
    int    last_camera_extra_heights_ = 0;

    int min_lookback_height_[BlockScene::kLaneCount]{};
    int earliest_traced_height_[BlockScene::kLaneCount]{};
    bool lookback_floors_valid_ = false;
    float initial_camera_scroll_z_ = 0.f;

    int poll_count_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;
    int stats_detail_refilled_ = 0;
    int stats_confirmed_marks_ = 0;
    int stats_dag_floods_ = 0;
    int stats_api_is_main_ = 0;
    int stats_uncles_checked_ = 0;
    int stats_uncles_removed_ = 0;
    int stats_fetch_admitted_ = 0;
    int stats_trace_missing_ = 0;
    int seed_lane_rr_ = 0;

    static constexpr size_t kMaxSeedQueue = 512;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxFloodPerSeed = 256;
    static constexpr int kMaxFetchAdmitsPerDrain = 4; // selection path only
    static constexpr int kMaxTraceEdges = 256;
    static constexpr int kMaxDfsNodes = 256;
};
