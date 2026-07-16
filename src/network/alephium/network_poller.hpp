#pragma once

// Thin network thread: owns curl + loop + block-fetch pool; policy in AlephiumAdapter.
#include "network/alephium/alephium_adapter.hpp"
#include "network/alephium/block_fetch_pool.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

class NetworkPoller
{
public:
    struct Config
    {
        std::string base_url;
        int64_t     lookback_ms      = 0;
        int64_t     poll_interval_ms = 8000;
    };

    NetworkPoller(BlockScene& scene, IEngine& engine);
    ~NetworkPoller();

    NetworkPoller(const NetworkPoller&) = delete;
    NetworkPoller& operator=(const NetworkPoller&) = delete;

    void start(const Config& cfg);
    void stop();

private:
    void thread_main();

    AlephiumAdapter   adapter_;
    BlockFetchPool    fetch_pool_;
    Config            cfg_{};
    std::thread       thread_;
    std::atomic<bool> running_{ false };

    static constexpr int kVerifyJobsPerIdleSlice = 24;
    static constexpr int kFetchWorkers = 3;
};
