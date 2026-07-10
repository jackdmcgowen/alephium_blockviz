#include "adapters/alephium/network_poller.hpp"

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

#include "alph_block.hpp"
#include "commands.h"

// commands.c process globals — owned exclusively by the network thread while running.
extern "C" CURL* curl;
extern const char* baseUrl;

NetworkPoller::NetworkPoller(VulkanRenderer& renderer)
    : renderer_(renderer)
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
    // Thread-local ownership of the global easy handle used by commands.c
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

    std::printf("[net] poller started url=%s interval_ms=%lld lookback_ms=%lld\n",
                cfg_.base_url.c_str(),
                static_cast<long long>(cfg_.poll_interval_ms),
                static_cast<long long>(cfg_.lookback_ms));

    while (running_)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        if (now - last_poll_ts >= cfg_.poll_interval_ms)
            poll_once(last_poll_ts);

        // Interruptible sleep so stop() is responsive
        for (int i = 0; i < 10 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (curl)
    {
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    std::printf("[net] poller stopped\n");
}

void NetworkPoller::poll_once(int64_t& last_poll_ts)
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    std::printf("\n[net] Polling blockflow from %lld to %lld\n",
                static_cast<long long>(last_poll_ts), static_cast<long long>(now));

    main_chain_cache_.refresh_tips();

    const int64_t from_ts = last_poll_ts - (ALPH_TARGET_BLOCK_SECONDS * 1000);
    cJSON* obj = get_blockflow_blocks_with_events(from_ts, now);
    if (!obj)
    {
        // Do not advance watermark on total failure (may retry same window)
        return;
    }

    GET_OBJECT_ITEM(obj, blocksAndEvents);
    if (blocksAndEvents && cJSON_IsArray(blocksAndEvents))
    {
        const int count = cJSON_GetArraySize(blocksAndEvents);
        int seen = 0, added = 0, skipped_not_main = 0, skipped_bad = 0;

        for (int i = 0; i < count; i++)
        {
            cJSON* shard = cJSON_GetArrayItem(blocksAndEvents, i);
            if (!shard || !cJSON_IsArray(shard))
                continue;

            const int n = cJSON_GetArraySize(shard);
            for (int j = 0; j < n; ++j)
            {
                cJSON* iter = cJSON_GetArrayItem(shard, j);
                GET_OBJECT_ITEM(iter, block);
                if (!block)
                    continue;

                ++seen;

                GET_OBJECT_ITEM(block, hash);
                GET_OBJECT_ITEM(block, height);
                GET_OBJECT_ITEM(block, chainFrom);
                GET_OBJECT_ITEM(block, chainTo);
                if (!hash || !cJSON_IsString(hash) || !hash->valuestring ||
                    !height || !chainFrom || !chainTo)
                {
                    ++skipped_bad;
                    continue;
                }

                const int h = height->valueint;
                const int cf = chainFrom->valueint;
                const int ct = chainTo->valueint;
                const std::string block_hash = hash->valuestring;

                if (!main_chain_cache_.ensure(block_hash, cf, ct, h))
                {
                    ++skipped_not_main;
                    continue;
                }

                renderer_.Add_Block(block);
                ++added;
            }
        }

        std::printf("[net] seen=%d added=%d skipped_not_main=%d skipped_bad=%d (confirmDepth=%d)\n",
                    seen, added, skipped_not_main, skipped_bad, ALPH_MAIN_CHAIN_CONFIRM_DEPTH);
    }

    last_poll_ts = now;
    cJSON_Delete(obj);
}
