#pragma once

// Domain/network policy for Alephium BlockFlow ingest (PR5/PR6b).
// Owns main-chain cache, verify queue, poll watermark logic.
// Writes into BlockScene; selection helpers go through IBlockvizEngine.
// NetworkPoller only owns the curl thread lifecycle.
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

    // Call once after curl is ready (network thread).
    void on_start();

    // One poll of blocks-with-events + optimistic admit + enqueue verify.
    void poll_once(int64_t& last_poll_ts);

    // Background main-chain verify / remove / replace + next-height cursor fetch.
    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    // PR11: rehydrate slim selection detail from API if engine requested it.
    void maybe_refill_selection_detail();

    size_t verify_queue_size() const { return verify_q_.size(); }
    int stats_verified_ok() const { return stats_verified_ok_; }
    int stats_removed() const { return stats_removed_; }
    int stats_replaced() const { return stats_replaced_; }
    int stats_detail_refilled() const { return stats_detail_refilled_; }
    int stats_confirmed_marks() const { return stats_confirmed_marks_; }
    int stats_cursor_advances() const { return stats_cursor_advances_; }
    int stats_next_height_fetches() const { return stats_next_height_fetches_; }

    int64_t poll_interval_ms() const { return cfg_.poll_interval_ms; }
    int64_t lookback_ms() const { return cfg_.lookback_ms; }

private:
    struct VerifyJob
    {
        std::string hash;
        int from = 0;
        int to = 0;
        int height = 0;
        bool hot = false;
    };

    void enqueue_verify(VerifyJob job);
    void verify_one(const VerifyJob& job);
    void prune_detail_store();
    // Scene-only dual-write; does NOT call mark_main. Prefer lane/height overload.
    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);

    void ensure_cursors_initialized_();
    // Fetch / verify / admit the block at H_c+1 for one lane; returns advances this call.
    int fetch_next_height_for_lane_(int from, int to);
    // Budgeted pass over all lanes needing H_c+1.
    void drain_next_heights_(int max_lane_jobs);

    bool admit_block_json_(cJSON* block_obj, const std::string& expected_hash,
                           int from, int to, int height);

    BlockScene& scene_;
    IBlockvizEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;

    std::deque<VerifyJob> verify_q_;
    std::unordered_set<std::string> verify_queued_;
    std::unordered_set<std::string> verify_done_;

    int poll_count_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;
    int stats_detail_refilled_ = 0;
    int stats_confirmed_marks_ = 0;
    int stats_cursor_advances_ = 0;
    int stats_next_height_fetches_ = 0;
    int next_height_lane_rr_ = 0; // round-robin start lane

    static constexpr size_t kMaxVerifyQueue = 50000;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxNextHeightPerDrain = 16; // all lanes each drain pass
};
