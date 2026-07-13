#include "adapters/alephium/alephium_adapter.hpp"

#include <algorithm>
#include <cjson/cJSON.h>
#include <cstdio>
#include <ctime>

#include "alph_block.hpp"
#include "commands.h"

namespace
{
uint32_t lane_of(int from, int to)
{
    return static_cast<uint32_t>(from * ALPH_NUM_GROUPS + to);
}

} // namespace

AlephiumAdapter::AlephiumAdapter(BlockScene& scene, IBlockvizEngine& engine)
    : scene_(scene)
    , engine_(engine)
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
    stats_detail_refilled_ = 0;
    stats_confirmed_marks_ = 0;
    stats_cursor_advances_ = 0;
    stats_next_height_fetches_ = 0;
    verify_q_.clear();
    verify_queued_.clear();
    verify_done_.clear();
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash)
{
    if (hash.empty())
        return;
    scene_.mark_confirmed(hash);
    ++stats_confirmed_marks_;
    // try_advance happens inside mark when graph has lane/height
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash, int from, int to, int height)
{
    if (hash.empty())
        return;
    const uint32_t lane = lane_of(from, to);
    scene_.mark_confirmed(hash, lane, height);
    ++stats_confirmed_marks_;
    const int steps = scene_.try_advance_confirmed(lane);
    stats_cursor_advances_ += steps;
}

void AlephiumAdapter::ensure_cursors_initialized_()
{
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    // Start near the live tip so we do not backfill tens of heights (queue congestion).
    // First target height = net_tip - kConfirmFetchHorizon; H_c = that - 1.
    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
        {
            const uint32_t lane = lane_of(f, t);
            if (scene_.cursor_initialized(lane))
                continue;
            const int net_tip = main_chain_cache_.tip(f, t);
            if (net_tip < 0)
                continue;
            const int start_h = std::max(0, net_tip - kConfirmFetchHorizon);
            // H_c = start_h - 1 so first advance targets start_h (within horizon of tip).
            scene_.ensure_cursor_initialized(lane, start_h - 1);
            std::printf("[adapter] cursor init lane %u (%d->%d) H_c=%d (start=%d net_tip=%d horizon=%d)\n",
                        lane, f, t, start_h - 1, start_h, net_tip, kConfirmFetchHorizon);
        }
    }
}

bool AlephiumAdapter::admit_block_json_(cJSON* block_obj, const std::string& expected_hash,
                                        int from, int to, int height)
{
    if (!block_obj)
        return false;

    cJSON* block = block_obj;
    if (!cJSON_GetObjectItem(block_obj, "hash"))
    {
        cJSON* inner = cJSON_GetObjectItem(block_obj, "block");
        if (inner)
            block = inner;
    }

    AlphBlock alph(block);
    if (alph.hash.empty())
        return false;
    if (!expected_hash.empty() && alph.hash != expected_hash)
        return false;

    scene_.add_block(block);
    main_chain_cache_.mark_main(alph.hash);
    mark_scene_confirmed_(alph.hash, from, to, height >= 0 ? height : alph.height);
    return true;
}

int AlephiumAdapter::fetch_next_height_for_lane_(int from, int to)
{
    const uint32_t lane = lane_of(from, to);
    if (!scene_.cursor_initialized(lane))
        return 0;

    // Snapshot H_c at call start — never request heights beyond H_c0 + horizon.
    const int h_c0 = scene_.confirmed_height(lane);
    const int max_request_h = h_c0 + kConfirmFetchHorizon;

    int total_steps = 0;
    int api_hops = 0;

    // Free advance through confirmed blocks already in the graph (no network).
    {
        const int steps = scene_.try_advance_confirmed(lane);
        if (steps > 0)
        {
            stats_cursor_advances_ += steps;
            total_steps += steps;
        }
    }

    // One network hop per call so drain_verify can round-robin across chains.
    // Horizon still caps which heights we will ever request (need <= H_c0 + 2).
    while (api_hops < 1)
    {
        const int h_c = scene_.confirmed_height(lane);
        const int need = h_c + 1;
        if (need < 0)
            break;
        // Do not request further than horizon from the start-of-call cursor
        // (and never more than 2 ahead of current H_c either).
        if (need > max_request_h || need > h_c + kConfirmFetchHorizon)
            break;

        const int net_tip = main_chain_cache_.tip(from, to);
        if (net_tip >= 0 && need > net_tip)
            break; // caught up to network tip

        // Prefer in-graph confirmed before hitting the API.
        {
            const int steps = scene_.try_advance_confirmed(lane);
            if (steps > 0)
            {
                stats_cursor_advances_ += steps;
                total_steps += steps;
                continue;
            }
        }

        ++stats_next_height_fetches_;
        ++api_hops;

        cJSON* arr = get_blockflow_hashes(from, to, need);
        if (!arr || !cJSON_IsArray(arr))
        {
            if (arr)
                cJSON_Delete(arr);
            break;
        }

        const int n = cJSON_GetArraySize(arr);
        std::string main_hash;
        if (n == 1)
        {
            cJSON* item = cJSON_GetArrayItem(arr, 0);
            if (item && cJSON_IsString(item) && item->valuestring)
                main_hash = item->valuestring;
        }
        else if (n > 1)
        {
            for (int i = 0; i < n; ++i)
            {
                cJSON* item = cJSON_GetArrayItem(arr, i);
                if (!item || !cJSON_IsString(item) || !item->valuestring)
                    continue;
                const std::string cand = item->valuestring;
                if (main_chain_cache_.is_cached_main(cand))
                {
                    main_hash = cand;
                    break;
                }
            }
            if (main_hash.empty())
            {
                for (int i = 0; i < n; ++i)
                {
                    cJSON* item = cJSON_GetArrayItem(arr, i);
                    if (!item || !cJSON_IsString(item) || !item->valuestring)
                        continue;
                    bool transport = false;
                    if (main_chain_cache_.query_is_main(item->valuestring, &transport) &&
                        transport)
                    {
                        main_hash = item->valuestring;
                        break;
                    }
                    if (!transport)
                        break;
                }
            }
        }
        cJSON_Delete(arr);

        if (main_hash.empty())
            break;

        const int h_before = scene_.confirmed_height(lane);

        if (!scene_.graph().contains(main_hash))
        {
            cJSON* block_obj = get_blockflow_blocks_blockhash(main_hash.c_str());
            if (!block_obj)
            {
                // Only enqueue verify within the same height horizon.
                if (need <= h_c0 + kConfirmFetchHorizon)
                {
                    VerifyJob job;
                    job.hash = main_hash;
                    job.from = from;
                    job.to = to;
                    job.height = need;
                    job.hot = true;
                    enqueue_verify(std::move(job));
                }
                break;
            }
            admit_block_json_(block_obj, main_hash, from, to, need);
            cJSON_Delete(block_obj);
        }
        else
        {
            main_chain_cache_.mark_main(main_hash);
            mark_scene_confirmed_(main_hash, from, to, need);
        }

        const int steps = scene_.try_advance_confirmed(lane);
        stats_cursor_advances_ += steps;
        total_steps += steps;

        if (scene_.confirmed_height(lane) <= h_before)
            break; // stuck on this height
    }
    return total_steps;
}

void AlephiumAdapter::drain_next_heights_(int max_lane_jobs)
{
    ensure_cursors_initialized_();
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    // One hop per lane, round-robin — never burn a drain on a single chain.
    const int n = std::max(1, std::min(max_lane_jobs, BlockScene::kLaneCount));
    for (int k = 0; k < n; ++k)
    {
        const int lane_i = next_height_lane_rr_ % BlockScene::kLaneCount;
        next_height_lane_rr_ = (next_height_lane_rr_ + 1) % BlockScene::kLaneCount;
        const int from = lane_i / ALPH_NUM_GROUPS;
        const int to = lane_i % ALPH_NUM_GROUPS;
        fetch_next_height_for_lane_(from, to);
    }
}

bool AlephiumAdapter::pop_verify_round_robin_(VerifyJob& out)
{
    if (verify_q_.empty())
        return false;

    // Prefer a job on the next preferred lane, then scan other lanes, then FIFO.
    for (int attempt = 0; attempt < BlockScene::kLaneCount; ++attempt)
    {
        const int want_lane =
            (verify_lane_rr_ + attempt) % BlockScene::kLaneCount;
        for (auto it = verify_q_.begin(); it != verify_q_.end(); ++it)
        {
            if (static_cast<int>(lane_of(it->from, it->to)) != want_lane)
                continue;
            out = std::move(*it);
            verify_q_.erase(it);
            verify_lane_rr_ = (want_lane + 1) % BlockScene::kLaneCount;
            return true;
        }
    }

    out = std::move(verify_q_.front());
    verify_q_.pop_front();
    verify_lane_rr_ =
        (static_cast<int>(lane_of(out.from, out.to)) + 1) % BlockScene::kLaneCount;
    return true;
}

void AlephiumAdapter::prune_detail_store()
{
    const size_t slimmed = scene_.detail_store().prune_unpinned_txns();
    if (slimmed == 0)
        return;
    const DetailStoreStats st = scene_.detail_store().stats();
    std::printf("[adapter] detail slim: pruned=%zu full=%zu slim=%zu total=%zu\n",
                slimmed, st.full_blocks, st.slim_blocks, st.entries);
}

void AlephiumAdapter::maybe_refill_selection_detail()
{
    const std::string hash = engine_.consume_detail_refill_request();
    if (hash.empty())
        return;

    // Already full (race with concurrent upsert) — refresh selection copy only.
    if (!scene_.detail_store().is_slim(hash))
    {
        if (engine_.is_selected(hash))
            engine_.set_selection(hash);
        return;
    }

    cJSON* block_obj = get_blockflow_blocks_blockhash(hash.c_str());
    if (!block_obj)
    {
        // Retry later
        engine_.set_selection(hash); // re-queues refill if still slim
        return;
    }

    cJSON* block = block_obj;
    if (!cJSON_GetObjectItem(block_obj, "hash"))
    {
        cJSON* inner = cJSON_GetObjectItem(block_obj, "block");
        if (inner)
            block = inner;
    }

    // Upsert full payload without re-admitting into the graph (may already be live).
    AlphBlock alph(block);
    if (!alph.hash.empty())
    {
        scene_.detail_store().upsert(alph);
        scene_.detail_store().set_full_detail_pin(alph.hash);
        ++stats_detail_refilled_;
        std::printf("[adapter] detail refilled %s txns=%zu\n",
                    alph.hash.c_str(), alph.txns.size());
        if (engine_.is_selected(alph.hash))
            engine_.set_selection(alph.hash);
    }

    cJSON_Delete(block_obj);
}

void AlephiumAdapter::on_start()
{
    main_chain_cache_.refresh_tips();
    ensure_cursors_initialized_();
}

bool AlephiumAdapter::ready_for_poll() const
{
    // Block new blockflow polls until the current set is fully confirmation-processed:
    // verify queue drained and every live cube is in the confirmed set (or gone).
    if (!verify_q_.empty())
        return false;
    if (scene_.unconfirmed_live_count() != 0)
        return false;
    return true;
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
        mark_scene_confirmed_(job.hash, job.from, job.to, job.height);
        return;
    }

    if (main_chain_cache_.try_hashes_singleton(job.hash, job.from, job.to, job.height))
    {
        verify_done_.insert(job.hash);
        ++stats_verified_ok_;
        mark_scene_confirmed_(job.hash, job.from, job.to, job.height);
        return;
    }

    bool transport_ok = false;
    if (main_chain_cache_.query_is_main(job.hash, &transport_ok))
    {
        verify_done_.insert(job.hash);
        ++stats_verified_ok_;
        mark_scene_confirmed_(job.hash, job.from, job.to, job.height);
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
    const bool reselect = engine_.is_selected(job.hash);
    scene_.remove_block(job.hash);
    if (reselect)
        engine_.clear_selection();
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

    scene_.add_block(block);
    // Always dual-write scene confirm for main_hash; ignore add_block bool.
    mark_scene_confirmed_(main_hash, job.from, job.to, job.height);
    ++stats_replaced_;
    if (reselect)
        engine_.set_selection(main_hash);
    std::printf("[adapter] replaced with main %s [%d->%d] h=%d%s\n",
                main_hash.c_str(), job.from, job.to, job.height,
                reselect ? " (reselected)" : "");

    cJSON_Delete(block_obj);
}

void AlephiumAdapter::drain_verify(int max_jobs, const std::atomic<bool>& running)
{
    maybe_refill_selection_detail();

    // Fair interleave: one next-height hop on one lane, then one verify job from
    // a different lane preference — so no single chain starves the others.
    int n = 0;
    while (n < max_jobs && running.load())
    {
        const bool did_height = [&]() {
            const size_t fetches_before = static_cast<size_t>(stats_next_height_fetches_);
            const int adv_before = stats_cursor_advances_;
            drain_next_heights_(/*max_lane_jobs=*/1);
            return stats_next_height_fetches_ > static_cast<int>(fetches_before) ||
                   stats_cursor_advances_ > adv_before;
        }();

        bool did_verify = false;
        VerifyJob job;
        if (pop_verify_round_robin_(job))
        {
            verify_one(job);
            did_verify = true;
        }

        if (!did_height && !did_verify)
            break;
        ++n;
    }
}

void AlephiumAdapter::poll_once(int64_t& last_poll_ts)
{
    // Prefer selection inspector continuity before long poll work
    maybe_refill_selection_detail();
    ensure_cursors_initialized_();

    // Safety: network poller should already gate on ready_for_poll().
    if (!ready_for_poll())
    {
        std::printf("[adapter] poll skipped — still confirming (q=%zu unconfirmed=%zu)\n",
                    verify_q_.size(), scene_.unconfirmed_live_count());
        return;
    }

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
    {
        maybe_refill_selection_detail();
        drain_next_heights_(BlockScene::kLaneCount);
        // Do not advance last_poll_ts on hard failure so we retry the window.
        return;
    }

    GET_OBJECT_ITEM(obj, blocksAndEvents);
    if (blocksAndEvents && cJSON_IsArray(blocksAndEvents))
    {
        const int count = cJSON_GetArraySize(blocksAndEvents);
        int seen = 0, added = 0, queued = 0, skipped_bad = 0, confirmed_now = 0;

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

                // Admit as unconfirmed; only mark confirmed after main-chain proof.
                const bool added_now = scene_.add_block(block);
                if (added_now)
                    ++added;

                if (main_chain_cache_.is_cached_main(block_hash))
                {
                    // Previously proven main (e.g. prior verify) — confirm now.
                    mark_scene_confirmed_(block_hash, cf, ct, h);
                    ++confirmed_now;
                }
                else if (!verify_done_.count(block_hash))
                {
                    // Always enqueue verify for every unconfirmed live block so
                    // ready_for_poll() can clear before the next poll.
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
                else if (scene_.graph().contains(block_hash) &&
                         !main_chain_cache_.is_cached_main(block_hash))
                {
                    // Live but still unconfirmed and verify_done said not-main was
                    // wrong / race — re-queue cold verify so poll is not stuck.
                    VerifyJob job;
                    job.hash = block_hash;
                    job.from = cf;
                    job.to = ct;
                    job.height = h;
                    job.hot = false;
                    verify_done_.erase(block_hash);
                    const size_t before = verify_q_.size();
                    enqueue_verify(std::move(job));
                    if (verify_q_.size() > before)
                        ++queued;
                }
                (void)added_now;
            }
        }

        std::printf("[adapter] seen=%d added=%d verify_queued+=%d confirmed_now=%d "
                    "skipped_bad=%d q=%zu unconfirmed=%zu verified_ok=%d removed=%d "
                    "replaced=%d cursor_adv=%d next_h_fetch=%d\n",
                    seen, added, queued, confirmed_now, skipped_bad, verify_q_.size(),
                    scene_.unconfirmed_live_count(), stats_verified_ok_, stats_removed_,
                    stats_replaced_, stats_cursor_advances_, stats_next_height_fetches_);
    }

    last_poll_ts = now;
    cJSON_Delete(obj);

    // One network hop per lane after poll (round-robin fairness).
    drain_next_heights_(BlockScene::kLaneCount);

    // PR11: drop txn payloads for unselected blocks (pin keeps selection full)
    prune_detail_store();
    maybe_refill_selection_detail();
}
