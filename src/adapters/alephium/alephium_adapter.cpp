#include "adapters/alephium/alephium_adapter.hpp"

#include <algorithm>
#include <cjson/cJSON.h>
#include <cstdio>
#include <ctime>
#include <queue>
#include <vector>

#include "alph_block.hpp"
#include "commands.h"

namespace
{
uint32_t lane_of(int from, int to)
{
    return static_cast<uint32_t>(from * ALPH_NUM_GROUPS + to);
}

cJSON* unwrap_block_json(cJSON* block_obj)
{
    if (!block_obj)
        return nullptr;
    if (cJSON_GetObjectItem(block_obj, "hash"))
        return block_obj;
    cJSON* inner = cJSON_GetObjectItem(block_obj, "block");
    return inner ? inner : block_obj;
}
} // namespace

AlephiumAdapter::AlephiumAdapter(BlockScene& scene, IBlockvizEngine& engine)
    : scene_(scene)
    , engine_(engine)
{
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
        min_lookback_height_[i] = 0;
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
    stats_dag_floods_ = 0;
    stats_api_is_main_ = 0;
    stats_uncles_checked_ = 0;
    stats_uncles_removed_ = 0;
    seed_q_.clear();
    seed_queued_.clear();
    proven_not_main_.clear();
    broken_dep_q_.clear();
    broken_dep_seen_.clear();
    uncle_q_.clear();
    uncle_queued_.clear();
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash)
{
    if (hash.empty())
        return;
    scene_.mark_confirmed(hash);
    ++stats_confirmed_marks_;
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash, int from, int to, int height)
{
    if (hash.empty())
        return;
    scene_.mark_confirmed(hash, lane_of(from, to), height);
    ++stats_confirmed_marks_;
}

void AlephiumAdapter::on_start()
{
    main_chain_cache_.refresh_tips();
    refresh_lookback_floors_();
}

void AlephiumAdapter::refresh_lookback_floors_()
{
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    // Heights older than ~lookback window are out of scope for DAG label / dep fetch.
    const int lookback_blocks =
        std::max(1, ALPH_LOOKBACK_WINDOW_SECONDS / ALPH_TARGET_BLOCK_SECONDS);

    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
        {
            const uint32_t lane = lane_of(f, t);
            const int net_tip = main_chain_cache_.tip(f, t);
            if (net_tip < 0)
            {
                min_lookback_height_[lane] = 0;
                continue;
            }
            min_lookback_height_[lane] = std::max(0, net_tip - lookback_blocks);
        }
    }
    lookback_floors_valid_ = true;
    std::printf("[adapter] lookback floors set (~%d heights behind tip, window=%ds)\n",
                lookback_blocks, ALPH_LOOKBACK_WINDOW_SECONDS);
}

bool AlephiumAdapter::height_in_lookback_(uint32_t lane, int height) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return false;
    if (height < 0)
        return true; // unknown height: allow offline label of live graph nodes only
    if (!lookback_floors_valid_)
        return true;
    return height >= min_lookback_height_[lane];
}

bool AlephiumAdapter::ready_for_poll() const
{
    if (!seed_q_.empty() || !uncle_q_.empty() || !broken_dep_q_.empty())
        return false;
    // Only blocks within the lookback height window must be confirmed before
    // the next poll (older live cubes do not gate).
    for (const GraphNode& n : scene_.nodes_snapshot())
    {
        if (!height_in_lookback_(n.lane, static_cast<int>(n.height)))
            continue;
        if (!main_chain_cache_.is_cached_main(n.id))
            return false;
    }
    return true;
}

void AlephiumAdapter::enqueue_seed_(SeedJob job)
{
    if (job.hash.empty())
        return;
    if (seed_queued_.count(job.hash) || proven_not_main_.count(job.hash))
        return;
    // Already confirmed — no work.
    if (main_chain_cache_.is_cached_main(job.hash))
    {
        mark_scene_confirmed_(job.hash, job.from, job.to, job.height);
        return;
    }

    if (seed_q_.size() >= kMaxSeedQueue)
    {
        if (!seed_q_.empty())
        {
            seed_queued_.erase(seed_q_.back().hash);
            seed_q_.pop_back();
        }
        if (seed_q_.size() >= kMaxSeedQueue)
            return;
    }

    seed_queued_.insert(job.hash);
    // Tips (higher height) first: push_front when height is hot.
    if (main_chain_cache_.is_hot_zone(job.from, job.to, job.height))
        seed_q_.push_front(std::move(job));
    else
        seed_q_.push_back(std::move(job));
}

bool AlephiumAdapter::pop_seed_round_robin_(SeedJob& out)
{
    if (seed_q_.empty())
        return false;

    for (int attempt = 0; attempt < BlockScene::kLaneCount; ++attempt)
    {
        const int want_lane = (seed_lane_rr_ + attempt) % BlockScene::kLaneCount;
        for (auto it = seed_q_.begin(); it != seed_q_.end(); ++it)
        {
            if (static_cast<int>(lane_of(it->from, it->to)) != want_lane)
                continue;
            out = std::move(*it);
            seed_queued_.erase(out.hash);
            seed_q_.erase(it);
            seed_lane_rr_ = (want_lane + 1) % BlockScene::kLaneCount;
            return true;
        }
    }

    out = std::move(seed_q_.front());
    seed_queued_.erase(out.hash);
    seed_q_.pop_front();
    seed_lane_rr_ =
        (static_cast<int>(lane_of(out.from, out.to)) + 1) % BlockScene::kLaneCount;
    return true;
}

bool AlephiumAdapter::fetch_and_admit_(const std::string& hash)
{
    if (hash.empty())
        return false;
    if (scene_.graph().contains(hash))
        return true;

    cJSON* block_obj = get_blockflow_blocks_blockhash(hash.c_str());
    if (!block_obj)
        return false;

    cJSON* block = unwrap_block_json(block_obj);
    AlphBlock alph(block);
    if (alph.hash.empty() || alph.hash != hash)
    {
        cJSON_Delete(block_obj);
        return false;
    }

    scene_.add_block(block);
    cJSON_Delete(block_obj);
    return scene_.graph().contains(hash);
}

int AlephiumAdapter::flood_confirm_deps_offline_(const std::string& main_hash, int budget)
{
    // Pure in-graph DAG label within lookback window. No is_main.
    // Missing dep of an in-window main-chain node → broken-link fetch only.
    if (main_hash.empty() || budget <= 0)
        return 0;

    std::queue<std::string> q;
    std::unordered_set<std::string> seen;
    q.push(main_hash);
    seen.insert(main_hash);
    int marked = 0;

    while (!q.empty() && marked < budget)
    {
        const std::string cur = std::move(q.front());
        q.pop();

        if (!scene_.graph().contains(cur))
        {
            // Only queue fetch when discovered as a dep of a main-chain walk
            // (caller only floods from proven main). Still no is_main on the dep.
            if (broken_dep_seen_.insert(cur).second)
                broken_dep_q_.push_back(cur);
            continue;
        }

        auto node = scene_.graph().get(cur);
        auto detail = scene_.detail_store().get(cur);
        const int from = node ? static_cast<int>(node->group_from) : 0;
        const int to = node ? static_cast<int>(node->group_to) : 0;
        const int height = node ? static_cast<int>(node->height) : -1;
        const uint32_t lane = node ? node->lane : lane_of(from, to);

        // Stop at lookback floor — do not label or walk further back.
        if (!height_in_lookback_(lane, height))
            continue;

        main_chain_cache_.mark_main(cur);
        mark_scene_confirmed_(cur, from, to, height);
        ++marked;
        ++stats_dag_floods_;

        if (!detail)
            continue;
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty() || !seen.insert(dep).second)
                continue;
            // If dep is live and below floor, skip without queuing a fetch.
            if (auto dn = scene_.graph().get(dep))
            {
                if (!height_in_lookback_(dn->lane, static_cast<int>(dn->height)))
                    continue;
            }
            // Missing dep: only fetch if parent is still in lookback window.
            if (!scene_.graph().contains(dep))
            {
                if (height_in_lookback_(lane, height) && broken_dep_seen_.insert(dep).second)
                    broken_dep_q_.push_back(dep);
                continue;
            }
            q.push(dep);
        }
    }
    return marked;
}

void AlephiumAdapter::label_all_confirmed_tip_ancestors_()
{
    // Fast offline pass: every current confirmed-height tip floods its dep DAG.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        const NodeId tip = scene_.confirmed_tip_hash(static_cast<uint32_t>(lane));
        if (tip.empty())
            continue;
        flood_confirm_deps_offline_(tip, kMaxFloodPerSeed);
    }
}

void AlephiumAdapter::enqueue_uncles_from_block_(const AlphBlock& alph)
{
    for (const std::string& unc : alph.uncles)
    {
        if (unc.empty() || uncle_queued_.count(unc) || proven_not_main_.count(unc))
            continue;
        if (main_chain_cache_.is_cached_main(unc))
        {
            // Main uncle: confirm (opaque cube ok) but Sobel only if it becomes frontier.
            mark_scene_confirmed_(unc);
            flood_confirm_deps_offline_(unc, kMaxFloodPerSeed);
            continue;
        }
        SeedJob uj;
        uj.hash = unc;
        uj.from = alph.chainFrom;
        uj.to = alph.chainTo;
        uj.height = alph.height; // uncle height often parent-1; refine after fetch
        uncle_queued_.insert(unc);
        uncle_q_.push_back(std::move(uj));
    }
}

void AlephiumAdapter::verify_uncle_(const std::string& uncle_hash, int parent_from,
                                   int parent_to, int parent_height)
{
    uncle_queued_.erase(uncle_hash);
    if (uncle_hash.empty())
        return;
    ++stats_uncles_checked_;

    // No individual block fetch for uncles — is_main is hash-only.
    // If not live and not main, nothing to remove; if live and not main, drop it.
    auto node = scene_.graph().get(uncle_hash);
    const int from = node ? static_cast<int>(node->group_from) : parent_from;
    const int to = node ? static_cast<int>(node->group_to) : parent_to;
    const int height = node ? static_cast<int>(node->height) : parent_height;
    const uint32_t lane = node ? node->lane : lane_of(from, to);

    if (node && !height_in_lookback_(lane, height))
    {
        // Outside lookback: drop from scene if present, no API.
        scene_.remove_block(uncle_hash);
        ++stats_uncles_removed_;
        return;
    }

    if (main_chain_cache_.is_cached_main(uncle_hash))
    {
        if (node)
        {
            mark_scene_confirmed_(uncle_hash, from, to, height);
            flood_confirm_deps_offline_(uncle_hash, kMaxFloodPerSeed);
        }
        return;
    }

    if (node && height >= 0 &&
        main_chain_cache_.try_hashes_singleton(uncle_hash, from, to, height))
    {
        mark_scene_confirmed_(uncle_hash, from, to, height);
        flood_confirm_deps_offline_(uncle_hash, kMaxFloodPerSeed);
        return;
    }

    bool transport_ok = false;
    ++stats_api_is_main_;
    if (main_chain_cache_.query_is_main(uncle_hash, &transport_ok))
    {
        if (node)
        {
            mark_scene_confirmed_(uncle_hash, from, to, height);
            flood_confirm_deps_offline_(uncle_hash, kMaxFloodPerSeed);
        }
        return;
    }

    if (!transport_ok)
    {
        SeedJob uj;
        uj.hash = uncle_hash;
        uj.from = from;
        uj.to = to;
        uj.height = height;
        if (uncle_queued_.insert(uncle_hash).second)
            uncle_q_.push_back(std::move(uj));
        return;
    }

    // Not main — remove uncle from the scene if present.
    std::printf("[adapter] uncle NOT main %s — remove\n", uncle_hash.c_str());
    if (node)
        scene_.remove_block(uncle_hash);
    proven_not_main_.insert(uncle_hash);
    ++stats_uncles_removed_;
    ++stats_removed_;
}

void AlephiumAdapter::replace_non_main_(const SeedJob& job)
{
    std::printf("[adapter] DAG seed NOT main %s [%d->%d] h=%d — remove\n",
                job.hash.c_str(), job.from, job.to, job.height);
    const bool reselect = engine_.is_selected(job.hash);
    scene_.remove_block(job.hash);
    if (reselect)
        engine_.clear_selection();
    ++stats_removed_;
    proven_not_main_.insert(job.hash);

    cJSON* arr = get_blockflow_hashes(job.from, job.to, job.height);
    if (!arr || !cJSON_IsArray(arr))
    {
        if (arr)
            cJSON_Delete(arr);
        return;
    }

    std::string main_hash;
    const int n = cJSON_GetArraySize(arr);
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
            ++stats_api_is_main_;
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

    if (!fetch_and_admit_(main_hash))
        return;

    main_chain_cache_.mark_main(main_hash);
    mark_scene_confirmed_(main_hash, job.from, job.to, job.height);
    flood_confirm_deps_offline_(main_hash, kMaxFloodPerSeed);
    label_all_confirmed_tip_ancestors_();
    ++stats_replaced_;
    ++stats_verified_ok_;
    if (reselect)
        engine_.set_selection(main_hash);
    std::printf("[adapter] replaced with main %s [%d->%d] h=%d\n",
                main_hash.c_str(), job.from, job.to, job.height);
}

void AlephiumAdapter::confirm_seed_(const SeedJob& seed)
{
    // Already resolved.
    if (main_chain_cache_.is_cached_main(seed.hash))
    {
        mark_scene_confirmed_(seed.hash, seed.from, seed.to, seed.height);
        flood_confirm_deps_offline_(seed.hash, kMaxFloodPerSeed);
        label_all_confirmed_tip_ancestors_();
        ++stats_verified_ok_;
        return;
    }

    // BFS ancestors from the seed (tip-first). For each unproven node, one is_main
    // query. On first main hit: offline flood all of its in-graph deps (no API).
    std::queue<std::string> walk;
    std::unordered_set<std::string> visited;
    walk.push(seed.hash);
    visited.insert(seed.hash);

    int walked = 0;
    while (!walk.empty() && walked < kMaxAncestorWalk)
    {
        const std::string cur = std::move(walk.front());
        walk.pop();
        ++walked;

        if (main_chain_cache_.is_cached_main(cur))
        {
            auto n = scene_.graph().get(cur);
            const int from = n ? static_cast<int>(n->group_from) : seed.from;
            const int to = n ? static_cast<int>(n->group_to) : seed.to;
            const int height = n ? static_cast<int>(n->height) : seed.height;
            mark_scene_confirmed_(cur, from, to, height);
            flood_confirm_deps_offline_(cur, kMaxFloodPerSeed);
            label_all_confirmed_tip_ancestors_();
            ++stats_verified_ok_;
            return;
        }

        // Never request individual blocks during the seed walk — only offline
        // flood of proven main may queue broken main-chain deps for fetch.
        if (!scene_.graph().contains(cur))
            continue;

        auto node = scene_.graph().get(cur);
        const int from = node ? static_cast<int>(node->group_from) : seed.from;
        const int to = node ? static_cast<int>(node->group_to) : seed.to;
        const int height = node ? static_cast<int>(node->height) : seed.height;
        const uint32_t lane = node ? node->lane : lane_of(from, to);

        // Do not is_main / walk below the initial lookback floor.
        if (!height_in_lookback_(lane, height))
            continue;

        // Singleton at height is often enough without full is_main.
        if (height >= 0 &&
            main_chain_cache_.try_hashes_singleton(cur, from, to, height))
        {
            mark_scene_confirmed_(cur, from, to, height);
            flood_confirm_deps_offline_(cur, kMaxFloodPerSeed);
            label_all_confirmed_tip_ancestors_();
            ++stats_verified_ok_;
            return;
        }

        bool transport_ok = false;
        ++stats_api_is_main_;
        if (main_chain_cache_.query_is_main(cur, &transport_ok))
        {
            mark_scene_confirmed_(cur, from, to, height);
            flood_confirm_deps_offline_(cur, kMaxFloodPerSeed);
            label_all_confirmed_tip_ancestors_();
            ++stats_verified_ok_;
            std::printf("[adapter] DAG hit main %s [%d->%d] h=%d — offline flood\n",
                        cur.c_str(), from, to, height);
            return;
        }

        if (!transport_ok)
        {
            SeedJob retry = seed;
            retry.hash = cur;
            retry.from = from;
            retry.to = to;
            retry.height = height;
            enqueue_seed_(std::move(retry));
            return;
        }

        // Not main: if this is the original tip seed, replace; else walk deps.
        if (cur == seed.hash)
        {
            replace_non_main_(seed);
            return;
        }

        if (auto d = scene_.detail_store().get(cur))
        {
            for (const std::string& dep : d->deps)
            {
                if (dep.empty() || !visited.insert(dep).second)
                    continue;
                if (auto dn = scene_.graph().get(dep))
                {
                    if (!height_in_lookback_(dn->lane, static_cast<int>(dn->height)))
                        continue;
                }
                walk.push(dep);
            }
        }
    }
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

    if (!scene_.detail_store().is_slim(hash))
    {
        if (engine_.is_selected(hash))
            engine_.set_selection(hash);
        return;
    }

    cJSON* block_obj = get_blockflow_blocks_blockhash(hash.c_str());
    if (!block_obj)
    {
        engine_.set_selection(hash);
        return;
    }

    cJSON* block = unwrap_block_json(block_obj);
    AlphBlock alph(block);
    if (!alph.hash.empty())
    {
        scene_.detail_store().upsert(alph);
        scene_.detail_store().set_full_detail_pin(alph.hash);
        ++stats_detail_refilled_;
        if (engine_.is_selected(alph.hash))
            engine_.set_selection(alph.hash);
    }
    cJSON_Delete(block_obj);
}

void AlephiumAdapter::drain_verify(int max_jobs, const std::atomic<bool>& running)
{
    maybe_refill_selection_detail();

    // Offline pass first: label all prior blocks of confirmed tips (no network).
    label_all_confirmed_tip_ancestors_();

    // Repair broken dep links (one fetch each), then offline flood again.
    int n = 0;
    while (n < max_jobs && running.load() && !broken_dep_q_.empty())
    {
        const std::string dep = std::move(broken_dep_q_.front());
        broken_dep_q_.pop_front();
        broken_dep_seen_.erase(dep);
        if (fetch_and_admit_(dep))
        {
            // Parent tips can now offline-flood through this link.
            label_all_confirmed_tip_ancestors_();
        }
        ++n;
    }

    // Manual uncle main-chain check (remove non-main uncles).
    while (n < max_jobs && running.load() && !uncle_q_.empty())
    {
        SeedJob uj = std::move(uncle_q_.front());
        uncle_q_.pop_front();
        verify_uncle_(uj.hash, uj.from, uj.to, uj.height);
        ++n;
    }

    // Re-seed from latest unconfirmed tips in the lookback window only.
    if (seed_q_.empty() && !ready_for_poll())
    {
        auto seed_node = [&](const NodeId& id) {
            auto n = scene_.graph().get(id);
            if (!n)
                return;
            if (!height_in_lookback_(n->lane, static_cast<int>(n->height)))
                return;
            if (main_chain_cache_.is_cached_main(id))
            {
                mark_scene_confirmed_(id, static_cast<int>(n->group_from),
                                      static_cast<int>(n->group_to),
                                      static_cast<int>(n->height));
                flood_confirm_deps_offline_(id, kMaxFloodPerSeed);
                return;
            }
            SeedJob job;
            job.hash = id;
            job.from = static_cast<int>(n->group_from);
            job.to = static_cast<int>(n->group_to);
            job.height = static_cast<int>(n->height);
            enqueue_seed_(std::move(job));
        };

        for (const NodeId& tip : scene_.tip_ids())
            seed_node(tip);

        if (seed_q_.empty())
        {
            for (const GraphNode& gn : scene_.nodes_snapshot())
            {
                if (main_chain_cache_.is_cached_main(gn.id))
                    continue;
                seed_node(gn.id);
            }
        }
    }

    while (n < max_jobs && running.load())
    {
        SeedJob job;
        if (!pop_seed_round_robin_(job))
            break;
        confirm_seed_(job);
        ++n;
    }

    // Final offline sweep after seeds.
    label_all_confirmed_tip_ancestors_();
}

void AlephiumAdapter::poll_once(int64_t& last_poll_ts)
{
    maybe_refill_selection_detail();

    if (!ready_for_poll())
    {
        std::printf("[adapter] poll skipped — DAG confirm pending (seeds=%zu unconfirmed=%zu)\n",
                    seed_q_.size(), scene_.unconfirmed_live_count());
        return;
    }

    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    std::printf("\n[adapter] Polling blockflow from %lld to %lld\n",
                static_cast<long long>(last_poll_ts), static_cast<long long>(now));

    ++poll_count_;
    if (poll_count_ == 1 || (poll_count_ % kTipRefreshEveryNPolls) == 0)
    {
        main_chain_cache_.refresh_tips();
        refresh_lookback_floors_();
    }

    const int64_t from_ts = last_poll_ts - (ALPH_TARGET_BLOCK_SECONDS * 1000);
    cJSON* obj = get_blockflow_blocks_with_events(from_ts, now);
    if (!obj)
    {
        maybe_refill_selection_detail();
        return;
    }

    GET_OBJECT_ITEM(obj, blocksAndEvents);
    int seen = 0, added = 0, seeded = 0, skipped_bad = 0;

    if (blocksAndEvents && cJSON_IsArray(blocksAndEvents))
    {
        const int count = cJSON_GetArraySize(blocksAndEvents);
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

                // Admit unconfirmed; confirmation is DAG work, not assume-main.
                const bool added_now = scene_.add_block(block);
                if (added_now)
                    ++added;

                // Queue ghost uncles for manual is_main (remove if not main).
                {
                    AlphBlock alph(block);
                    if (!alph.hash.empty())
                        enqueue_uncles_from_block_(alph);
                }

                if (main_chain_cache_.is_cached_main(block_hash))
                {
                    mark_scene_confirmed_(block_hash, cf, ct, h);
                    flood_confirm_deps_offline_(block_hash, kMaxFloodPerSeed);
                }
                else
                {
                    SeedJob job;
                    job.hash = block_hash;
                    job.from = cf;
                    job.to = ct;
                    job.height = h;
                    const size_t before = seed_q_.size();
                    enqueue_seed_(std::move(job));
                    if (seed_q_.size() > before)
                        ++seeded;
                }
            }
        }
    }

    // Prefer current tips as seeds (highest priority for backward search).
    for (const NodeId& tip : scene_.tip_ids())
    {
        auto n = scene_.graph().get(tip);
        if (!n)
            continue;
        SeedJob job;
        job.hash = tip;
        job.from = static_cast<int>(n->group_from);
        job.to = static_cast<int>(n->group_to);
        job.height = static_cast<int>(n->height);
        enqueue_seed_(std::move(job));
    }

    std::printf("[adapter] seen=%d added=%d seeds+=%d skipped_bad=%d "
                "seed_q=%zu uncle_q=%zu unconfirmed=%zu is_main_api=%d floods=%d "
                "uncles_checked=%d uncles_rm=%d confirmed_marks=%d removed=%d\n",
                seen, added, seeded, skipped_bad, seed_q_.size(), uncle_q_.size(),
                scene_.unconfirmed_live_count(), stats_api_is_main_, stats_dag_floods_,
                stats_uncles_checked_, stats_uncles_removed_, stats_confirmed_marks_,
                stats_removed_);

    last_poll_ts = now;
    cJSON_Delete(obj);

    prune_detail_store();
    maybe_refill_selection_detail();
}
