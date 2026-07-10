#pragma once

// Domain/network policy for Alephium BlockFlow ingest (PR5/PR6b).
// Owns main-chain cache, verify queue, poll watermark logic.
// Writes into BlockScene; selection helpers go through VulkanEngine.
// NetworkPoller only owns the curl thread lifecycle.
#include "adapters/alephium/main_chain_cache.hpp"
#include "domain/block_scene.hpp"
#include "engine/vulkan_engine.hpp"

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

    AlephiumAdapter(BlockScene& scene, VulkanEngine& engine);

    void configure(const Config& cfg);
    void reset_stats();

    // Call once after curl is ready (network thread).
    void on_start();

    // One poll of blocks-with-events + optimistic admit + enqueue verify.
    void poll_once(int64_t& last_poll_ts);

    // Background main-chain verify / remove / replace.
    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    // PR11: rehydrate slim selection detail from API if engine requested it.
    void maybe_refill_selection_detail();

    size_t verify_queue_size() const { return verify_q_.size(); }
    int stats_verified_ok() const { return stats_verified_ok_; }
    int stats_removed() const { return stats_removed_; }
    int stats_replaced() const { return stats_replaced_; }
    int stats_detail_refilled() const { return stats_detail_refilled_; }

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

    BlockScene& scene_;
    VulkanEngine& engine_;
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

    static constexpr size_t kMaxVerifyQueue = 50000;
    static constexpr int kTipRefreshEveryNPolls = 3;
};
