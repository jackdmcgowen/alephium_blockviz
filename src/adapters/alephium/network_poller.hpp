#pragma once

// Network thread: poll (fast optimistic admit) + background main-chain verify/replace.
#include "adapters/alephium/main_chain_cache.hpp"
#include "vulkan_renderer.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <unordered_set>

class NetworkPoller
{
public:
    struct Config
    {
        std::string base_url;
        int64_t     lookback_ms      = 0;
        int64_t     poll_interval_ms = 8000;
    };

    explicit NetworkPoller(VulkanRenderer& renderer);
    ~NetworkPoller();

    NetworkPoller(const NetworkPoller&) = delete;
    NetworkPoller& operator=(const NetworkPoller&) = delete;

    void start(const Config& cfg);
    void stop();

private:
    struct VerifyJob
    {
        std::string hash;
        int from = 0;
        int to = 0;
        int height = 0;
        bool hot = false; // tip zone — higher priority
    };

    void thread_main();
    void poll_once(int64_t& last_poll_ts);
    void enqueue_verify(VerifyJob job);
    void drain_verify(int max_jobs);
    void verify_one(const VerifyJob& job);

    VulkanRenderer&   renderer_;
    Config            cfg_{};
    std::thread       thread_;
    std::atomic<bool> running_{ false };
    MainChainCache    main_chain_cache_;

    std::deque<VerifyJob> verify_q_;
    std::unordered_set<std::string> verify_queued_; // currently in queue
    std::unordered_set<std::string> verify_done_;   // verified main (or handled)

    int poll_count_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;

    static constexpr size_t kMaxVerifyQueue = 50000;
    static constexpr int    kTipRefreshEveryNPolls = 3;
    static constexpr int    kVerifyJobsPerIdleSlice = 8;
};
