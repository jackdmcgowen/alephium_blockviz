#pragma once

// Network thread: owns all curl / blockflow HTTP. UI and render threads never call commands.c.
#include "adapters/alephium/main_chain_cache.hpp"
#include "vulkan_renderer.hpp"

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

    explicit NetworkPoller(VulkanRenderer& renderer);
    ~NetworkPoller();

    NetworkPoller(const NetworkPoller&) = delete;
    NetworkPoller& operator=(const NetworkPoller&) = delete;

    void start(const Config& cfg);
    void stop();

private:
    void thread_main();
    void poll_once(int64_t& last_poll_ts);

    VulkanRenderer&      renderer_;
    Config               cfg_{};
    std::thread          thread_;
    std::atomic<bool>    running_{ false };
    MainChainCache       main_chain_cache_;
};
