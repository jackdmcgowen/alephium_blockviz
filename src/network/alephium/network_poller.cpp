#include "network/pch.h"
#include "network/alephium/network_poller.hpp"
#include "domain/alph_block.hpp"

#include <curl/curl.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

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

        // Retention: keep all loaded segment blocks until hard memory pressure.
        // Sliding ring only controls view/fetch; adapter may also prune at hard cap.
        // Last-resort drop oldest nodes (~2 GB / node soft max) with a clear warning.
        {
            static constexpr size_t kMemCapBytes = 2ull * 1024 * 1024 * 1024;
            static constexpr size_t kSoftMaxNodes = 250000;
            size_t private_bytes = 0;
#if defined(_WIN32)
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(),
                                     reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                     sizeof(pmc)))
                private_bytes = static_cast<size_t>(pmc.PrivateUsage);
#endif
            const size_t nodes = scene_.total_blocks();
            const bool over_mem = private_bytes > 0 && private_bytes >= kMemCapBytes;
            const bool over_nodes = nodes >= kSoftMaxNodes;
            if (over_mem || over_nodes)
            {
                size_t cap = kSoftMaxNodes * 9 / 10;
                if (over_mem && nodes > 1000)
                    cap = (std::min)(cap, nodes * 85 / 100);
                if (over_nodes)
                    cap = (std::min)(cap, kSoftMaxNodes * 9 / 10);
                const size_t removed = scene_.prune(/*min_timestamp_ms=*/0, cap);
                if (removed > 0)
                    std::printf("[net] WARNING timeline cache full — pruned oldest %zu blocks "
                                "(mem=%zu MB nodes=%zu -> cap %zu). "
                                "Future: disk cache to keep history across runs.\n",
                                removed, private_bytes / (1024 * 1024), nodes, cap);
            }
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
