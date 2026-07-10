#pragma once

// Domain/network policy for Alephium BlockFlow ingest (PR5).
// Owns main-chain cache, verify queue, poll watermark logic.
// NetworkPoller only owns the curl thread lifecycle.
#include "adapters/alephium/main_chain_cache.hpp"
#include "vulkan_renderer.hpp"

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

    explicit AlephiumAdapter(VulkanRenderer& renderer);

    void configure(const Config& cfg);
    void reset_stats();

    // Call once after curl is ready (network thread).
    void on_start();

    // One poll of blocks-with-events + optimistic admit + enqueue verify.
    void poll_once(int64_t& last_poll_ts);

    // Background main-chain verify / remove / replace.
    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    size_t verify_queue_size() const { return verify_q_.size(); }
    int stats_verified_ok() const { return stats_verified_ok_; }
    int stats_removed() const { return stats_removed_; }
    int stats_replaced() const { return stats_replaced_; }

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

    VulkanRenderer& renderer_;
    Config cfg_{};
    MainChainCache main_chain_cache_;

    std::deque<VerifyJob> verify_q_;
    std::unordered_set<std::string> verify_queued_;
    std::unordered_set<std::string> verify_done_;

    int poll_count_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;

    static constexpr size_t kMaxVerifyQueue = 50000;
    static constexpr int kTipRefreshEveryNPolls = 3;
};
