#pragma once

// Domain/network policy for Alephium BlockFlow ingest.
// Confirmation: is_main only on live tips; then offline flood of in-graph deps
// within the lookback height window (no ancestor is_main walk).
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

    void on_start();

    // Admit latest blocks; seed DAG confirm from live tips. Call only when ready_for_poll().
    void poll_once(int64_t& last_poll_ts);

    // True when live tips are confirmed and seed/uncle work is idle.
    bool ready_for_poll() const;

    // Drain tip-first DAG confirmation (round-robin seeds).
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
    // is_main on the tip only; offline flood in-graph deps within lookback.
    void confirm_seed_(const SeedJob& seed);
    // Offline: mark main_hash + in-graph deps at/above lookback floor (no network).
    int flood_confirm_deps_offline_(const std::string& main_hash, int budget);
    // Fetch only for a missing dep of an already-proven main tip (broken link).
    bool fetch_and_admit_(const std::string& hash);
    void replace_non_main_(const SeedJob& job);
    void verify_uncle_(const std::string& uncle_hash, int parent_from, int parent_to,
                       int parent_height);
    void enqueue_uncles_from_block_(const AlphBlock& alph);
    void label_all_confirmed_tip_ancestors_();
    void refresh_lookback_floors_();
    bool height_in_lookback_(uint32_t lane, int height) const;
    bool tip_pending_confirmation_() const;

    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);
    void prune_detail_store();

    BlockScene& scene_;
    IBlockvizEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;

    std::deque<SeedJob> seed_q_;
    std::unordered_set<std::string> seed_queued_;
    std::unordered_set<std::string> proven_not_main_;
    // Missing deps of proven main tips only; one-shot fetch then drop if fail.
    std::deque<std::string> broken_dep_q_;
    std::unordered_set<std::string> broken_dep_seen_;
    std::unordered_set<std::string> broken_dep_failed_; // never re-fetch
    std::deque<SeedJob> uncle_q_;
    std::unordered_set<std::string> uncle_queued_;

    int min_lookback_height_[BlockScene::kLaneCount]{};
    bool lookback_floors_valid_ = false;

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
    int seed_lane_rr_ = 0;

    static constexpr size_t kMaxSeedQueue = 512;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxFloodPerSeed = 256;
    static constexpr int kMaxBrokenFetchesPerDrain = 4;
};
