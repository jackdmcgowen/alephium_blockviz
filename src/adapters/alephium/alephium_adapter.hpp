#pragma once

// Domain/network policy for Alephium BlockFlow ingest.
// Confirmation: tip-first backward DAG walk — prove one main-chain block via API,
// then flood-confirm its dependency closure (fetch missing deps; replace imposters).
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

    // True when every live block is confirmed (DAG work finished).
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
    // Backward DAG search from a tip/seed; one is_main per unproven ancestor until hit.
    void confirm_seed_(const SeedJob& seed);
    // Mark hash + transitive deps confirmed (no is_main API). Fetch missing deps.
    int flood_confirm_deps_(const std::string& main_hash, int budget);
    // Fetch block by hash and admit unconfirmed (for missing deps).
    bool fetch_and_admit_(const std::string& hash);
    // Non-main tip: remove and try admit main at that height.
    void replace_non_main_(const SeedJob& job);

    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);
    void prune_detail_store();

    BlockScene& scene_;
    IBlockvizEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;

    // Seeds = tips / unconfirmed roots to walk (not every block).
    std::deque<SeedJob> seed_q_;
    std::unordered_set<std::string> seed_queued_;
    std::unordered_set<std::string> proven_not_main_; // avoid re-query thrash

    int poll_count_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;
    int stats_detail_refilled_ = 0;
    int stats_confirmed_marks_ = 0;
    int stats_dag_floods_ = 0;
    int stats_api_is_main_ = 0;
    int seed_lane_rr_ = 0;

    static constexpr size_t kMaxSeedQueue = 4096;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxAncestorWalk = 64;
    static constexpr int kMaxFloodPerSeed = 256;
};
