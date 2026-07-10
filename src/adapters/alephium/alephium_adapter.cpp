#include "adapters/alephium/alephium_adapter.hpp"

#include <cjson/cJSON.h>
#include <cstdio>
#include <ctime>

#include "alph_block.hpp"
#include "commands.h"

AlephiumAdapter::AlephiumAdapter(VulkanRenderer& renderer)
    : renderer_(renderer)
{
}

void AlephiumAdapter::configure(const Config& cfg)
{
    cfg_ = cfg;
}

void AlephiumAdapter::reset_stats()
{
    poll_count_ = 0;
    stats_verified_ok_ = stats_removed_ = stats_replaced_ = 0;
    verify_q_.clear();
    verify_queued_.clear();
    verify_done_.clear();
}

void AlephiumAdapter::on_start()
{
    main_chain_cache_.refresh_tips();
}

void AlephiumAdapter::enqueue_verify(VerifyJob job)
{
    if (job.hash.empty())
        return;
    if (main_chain_cache_.is_cached_main(job.hash))
        return;
    if (verify_done_.count(job.hash) || verify_queued_.count(job.hash))
        return;

    if (verify_q_.size() >= kMaxVerifyQueue)
    {
        if (!verify_q_.empty() && !verify_q_.back().hot)
        {
            verify_queued_.erase(verify_q_.back().hash);
            verify_q_.pop_back();
        }
        if (verify_q_.size() >= kMaxVerifyQueue)
            return;
    }

    verify_queued_.insert(job.hash);
    if (job.hot)
        verify_q_.push_front(std::move(job));
    else
        verify_q_.push_back(std::move(job));
}

void AlephiumAdapter::verify_one(const VerifyJob& job)
{
    verify_queued_.erase(job.hash);

    if (main_chain_cache_.is_cached_main(job.hash))
    {
        verify_done_.insert(job.hash);
        ++stats_verified_ok_;
        return;
    }

    if (main_chain_cache_.try_hashes_singleton(job.hash, job.from, job.to, job.height))
    {
        verify_done_.insert(job.hash);
        ++stats_verified_ok_;
        return;
    }

    bool transport_ok = false;
    if (main_chain_cache_.query_is_main(job.hash, &transport_ok))
    {
        verify_done_.insert(job.hash);
        ++stats_verified_ok_;
        return;
    }

    if (!transport_ok)
    {
        VerifyJob retry = job;
        retry.hot = false;
        enqueue_verify(std::move(retry));
        return;
    }

    std::printf("[adapter] verify NOT main %s [%d->%d] h=%d — remove\n",
                job.hash.c_str(), job.from, job.to, job.height);
    const bool reselect = renderer_.is_selected(job.hash);
    renderer_.Remove_Block(job.hash);
    ++stats_removed_;
    verify_done_.insert(job.hash);

    cJSON* arr = get_blockflow_hashes(job.from, job.to, job.height);
    if (!arr || !cJSON_IsArray(arr))
    {
        if (arr)
            cJSON_Delete(arr);
        return;
    }

    const int n = cJSON_GetArraySize(arr);
    std::string main_hash;
    if (n == 1)
    {
        cJSON* item = cJSON_GetArrayItem(arr, 0);
        if (item && cJSON_IsString(item) && item->valuestring)
            main_hash = item->valuestring;
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (!item || !cJSON_IsString(item) || !item->valuestring)
                continue;
            const std::string cand = item->valuestring;
            if (cand == job.hash)
                continue;
            bool transport = false;
            if (main_chain_cache_.query_is_main(cand, &transport) && transport)
            {
                main_hash = cand;
                break;
            }
        }
    }
    cJSON_Delete(arr);

    if (main_hash.empty() || main_hash == job.hash)
        return;

    main_chain_cache_.mark_main(main_hash);
    verify_done_.insert(main_hash);

    cJSON* block_obj = get_blockflow_blocks_blockhash(main_hash.c_str());
    if (!block_obj)
        return;

    cJSON* block = block_obj;
    if (!cJSON_GetObjectItem(block_obj, "hash"))
    {
        cJSON* inner = cJSON_GetObjectItem(block_obj, "block");
        if (inner)
            block = inner;
    }

    renderer_.Add_Block(block);
    ++stats_replaced_;
    if (reselect)
        renderer_.set_selection(main_hash);
    std::printf("[adapter] replaced with main %s [%d->%d] h=%d%s\n",
                main_hash.c_str(), job.from, job.to, job.height,
                reselect ? " (reselected)" : "");

    cJSON_Delete(block_obj);
}

void AlephiumAdapter::drain_verify(int max_jobs, const std::atomic<bool>& running)
{
    int n = 0;
    while (n < max_jobs && running.load() && !verify_q_.empty())
    {
        VerifyJob job = std::move(verify_q_.front());
        verify_q_.pop_front();
        verify_one(job);
        ++n;
    }
}

void AlephiumAdapter::poll_once(int64_t& last_poll_ts)
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    std::printf("\n[adapter] Polling blockflow from %lld to %lld (verify_q=%zu)\n",
                static_cast<long long>(last_poll_ts), static_cast<long long>(now),
                verify_q_.size());

    ++poll_count_;
    if (poll_count_ == 1 || (poll_count_ % kTipRefreshEveryNPolls) == 0)
        main_chain_cache_.refresh_tips();

    const int64_t from_ts = last_poll_ts - (ALPH_TARGET_BLOCK_SECONDS * 1000);
    cJSON* obj = get_blockflow_blocks_with_events(from_ts, now);
    if (!obj)
        return;

    GET_OBJECT_ITEM(obj, blocksAndEvents);
    if (blocksAndEvents && cJSON_IsArray(blocksAndEvents))
    {
        const int count = cJSON_GetArraySize(blocksAndEvents);
        int seen = 0, added = 0, queued = 0, skipped_bad = 0;

        for (int i = 0; i < count; i++)
        {
            cJSON* shard = cJSON_GetArrayItem(blocksAndEvents, i);
            if (!shard || !cJSON_IsArray(shard))
                continue;

            const int bn = cJSON_GetArraySize(shard);
            for (int j = 0; j < bn; ++j)
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

                renderer_.Add_Block(block);
                ++added;

                if (!main_chain_cache_.is_cached_main(block_hash) &&
                    !verify_done_.count(block_hash))
                {
                    VerifyJob job;
                    job.hash = block_hash;
                    job.from = cf;
                    job.to = ct;
                    job.height = h;
                    job.hot = main_chain_cache_.is_hot_zone(cf, ct, h);
                    const size_t before = verify_q_.size();
                    enqueue_verify(std::move(job));
                    if (verify_q_.size() > before)
                        ++queued;
                }
            }
        }

        std::printf("[adapter] seen=%d added=%d verify_queued+=%d skipped_bad=%d "
                    "q=%zu verified_ok=%d removed=%d replaced=%d\n",
                    seen, added, queued, skipped_bad, verify_q_.size(),
                    stats_verified_ok_, stats_removed_, stats_replaced_);
    }

    last_poll_ts = now;
    cJSON_Delete(obj);
}
