#pragma once

// Domain/network policy for Alephium BlockFlow ingest.
// Confirmation: is_main only on live tips; offline flood + completeness trace
// with multi-thread block-fetch for missing deps within lookback.
#include "adapters/alephium/block_fetch_pool.hpp"
#include "adapters/alephium/main_chain_cache.hpp"
#include "domain/block_scene.hpp"
#include "engine/blockviz_engine_api.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

class AlephiumAdapter
{
public:
    struct Config
    {
        int64_t lookback_ms = 0;
        int64_t poll_interval_ms = 8000;
    };

    AlephiumAdapter(BlockScene& scene, IBlockvizEngine& engine);

    void configure(const Config& cfg);
    void reset_stats();

    // Optional external fetch pool (owned by NetworkPoller). Not owned here.
    void set_fetch_pool(BlockFetchPool* pool) { fetch_pool_ = pool; }

    void on_start();

    // Admit latest blocks; seed DAG confirm from live tips.
    void poll_once(int64_t& last_poll_ts);

    bool ready_for_poll() const;

    // Drain tip is_main + completeness traces + fetch admits.
    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    void maybe_refill_selection_detail();

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
    int flood_confirm_deps_offline_(const std::string& main_hash, int budget);
    int maybe_flood_offline_(const std::string& main_hash, uint32_t lane, int height,
                             bool force);
    bool lane_needs_reflood_(uint32_t lane) const;
    bool fetch_and_admit_(const std::string& hash); // sync fallback (selection etc.)
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

    // Same-chain completeness: tip → floor; enqueue missing deps to fetch pool.
    void kick_completeness_trace_(const std::string& tip_hash);
    int  run_completeness_trace_(const std::string& start_hash, int budget);
    void drain_fetch_results_(int max_admits);
    bool enqueue_missing_dep_(const std::string& dep_hash);
    int  min_live_height_on_lane_(uint32_t lane) const;

    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);
    void prune_detail_store();

    BlockScene& scene_;
    IBlockvizEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;
    BlockFetchPool* fetch_pool_ = nullptr; // not owned

    std::deque<SeedJob> seed_q_;
    std::unordered_set<std::string> seed_queued_;
    std::unordered_set<std::string> proven_not_main_;
    std::deque<std::string> broken_dep_q_;
    std::unordered_set<std::string> broken_dep_seen_;
    std::unordered_set<std::string> broken_dep_failed_;
    std::deque<SeedJob> uncle_q_;
    std::unordered_set<std::string> uncle_queued_;
    // Lanes needing another completeness pass (after fetch admits / new confirm).
    std::unordered_set<uint32_t> completeness_pending_lanes_;

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
    static constexpr int kMaxBrokenFetchesPerDrain = 4;
    static constexpr int kMaxCompletenessNodes = 128;
    static constexpr int kMaxFetchAdmitsPerDrain = 16;
};
