#include "adapters/alephium/network_poller.hpp"

#include <curl/curl.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

extern "C" CURL* curl;
extern const char* baseUrl;

NetworkPoller::NetworkPoller(BlockScene& scene, IBlockvizEngine& engine)
    : adapter_(scene, engine)
{
}

NetworkPoller::~NetworkPoller()
{
    stop();
}

void NetworkPoller::start(const Config& cfg)
{
    stop();
    cfg_ = cfg;
    adapter_.configure({ cfg.lookback_ms, cfg.poll_interval_ms });
    adapter_.reset_stats();
    running_ = true;
    thread_ = std::thread(&NetworkPoller::thread_main, this);
}

void NetworkPoller::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

void NetworkPoller::thread_main()
{
    baseUrl = cfg_.base_url.c_str();
    curl = curl_easy_init();
    if (!curl)
    {
        std::printf("[net] curl_easy_init failed\n");
        running_ = false;
        return;
    }

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    int64_t last_poll_ts =
        static_cast<int64_t>(std::time(nullptr)) * 1000 - cfg_.lookback_ms;

    std::printf("[net] poller started url=%s (adapter-owned policy)\n", cfg_.base_url.c_str());
    adapter_.on_start();

    while (running_)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        // Never poll blockflow until every currently live block is confirmed
        // (or removed as non-main) and the verify queue is empty.
        if (adapter_.ready_for_poll() &&
            now - last_poll_ts >= adapter_.poll_interval_ms())
        {
            adapter_.poll_once(last_poll_ts);
        }

        // Prefer draining confirmation work over admitting more blocks.
        adapter_.drain_verify(kVerifyJobsPerIdleSlice, running_);

        for (int i = 0; i < 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (curl)
    {
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    std::printf("[net] poller stopped (verified_ok=%d removed=%d replaced=%d refilled=%d queue=%zu)\n",
                adapter_.stats_verified_ok(), adapter_.stats_removed(),
                adapter_.stats_replaced(), adapter_.stats_detail_refilled(),
                adapter_.verify_queue_size());
}
