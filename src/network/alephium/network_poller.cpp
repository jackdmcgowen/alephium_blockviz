#include "network/pch.h"
#include "network/alephium/network_poller.hpp"
#include "domain/alph_block.hpp"

#include <curl/curl.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

extern "C" CURL* curl;
extern const char* baseUrl;

NetworkPoller::NetworkPoller(BlockScene& scene, IEngine& engine)
    : scene_(scene)
    , adapter_(scene, engine)
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
    base_url_copy_ = cfg.base_url;
    adapter_.configure({ cfg.lookback_ms, cfg.poll_interval_ms });
    adapter_.full_reset();
    fetch_pool_.start(cfg.base_url, kFetchWorkers);
    adapter_.set_fetch_pool(&fetch_pool_);
    running_ = true;
    thread_ = std::thread(&NetworkPoller::thread_main, this);
}

void NetworkPoller::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    adapter_.set_fetch_pool(nullptr);
    fetch_pool_.stop();
}

void NetworkPoller::prepare_domain_switch()
{
    adapter_.full_reset();
}

void NetworkPoller::set_domain_meta(int domain, const std::string& base_url)
{
    domain_ = domain;
    base_url_copy_ = base_url;
    cfg_.base_url = base_url;
}

void NetworkPoller::thread_main()
{
    baseUrl = base_url_copy_.c_str();
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

    std::printf("[net] poller started url=%s (adapter-owned policy + fetch pool)\n",
                base_url_copy_.c_str());
    adapter_.on_start();
    adapter_.publish_hud(domain_, base_url_copy_.c_str(), /*switching=*/false);

    while (running_)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        // Bootstrap: first poll only. IdentifyTips/DfsTrace: no new window until ready.
        // Steady: normal interval polls.
        if (adapter_.ready_for_poll() &&
            (adapter_.phase() == AlephiumAdapter::Phase::BootstrapPoll ||
             now - last_poll_ts >= adapter_.poll_interval_ms()))
            adapter_.poll_once(last_poll_ts);

        // Tip is_main, per-chain DFS, fetch admits.
        adapter_.drain_verify(kVerifyJobsPerIdleSlice, running_);

        // Retention: keep ~2× lookback window and a soft node cap (protects frontier tips).
        {
            const int64_t look = adapter_.lookback_ms() > 0
                                     ? adapter_.lookback_ms()
                                     : static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
            const int64_t min_ts = now - look * 2;
            const size_t removed = scene_.prune(min_ts, /*max_nodes=*/12000);
            if (removed > 0)
                std::printf("[net] prune removed=%zu (min_ts window 2x lookback)\n", removed);
        }

        adapter_.publish_hud(domain_, base_url_copy_.c_str(), /*switching=*/false);

        for (int i = 0; i < 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (curl)
    {
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    std::printf("[net] poller stopped (verified_ok=%d removed=%d replaced=%d refilled=%d "
                "seeds=%zu is_main_api=%d fetch_adm=%d)\n",
                adapter_.stats_verified_ok(), adapter_.stats_removed(),
                adapter_.stats_replaced(), adapter_.stats_detail_refilled(),
                adapter_.verify_queue_size(), adapter_.stats_api_is_main(),
                adapter_.stats_fetch_admitted());
}
