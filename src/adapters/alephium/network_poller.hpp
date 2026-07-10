#pragma once

// Thin network thread: owns curl + loop; delegates policy to AlephiumAdapter (PR5).
#include "adapters/alephium/alephium_adapter.hpp"
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

    VulkanRenderer&   renderer_;
    AlephiumAdapter   adapter_;
    Config            cfg_{};
    std::thread       thread_;
    std::atomic<bool> running_{ false };

    static constexpr int kVerifyJobsPerIdleSlice = 8;
};
