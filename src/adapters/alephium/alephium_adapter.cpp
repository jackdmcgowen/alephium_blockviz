#include "adapters/alephium/alephium_adapter.hpp"

#include <algorithm>
#include <cjson/cJSON.h>
#include <climits>
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
    , initial_camera_scroll_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
{
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        min_lookback_height_[i] = 0;
        earliest_traced_height_[i] = INT_MAX; // none traced yet
        lockstep_tip_height_[i] = -1;
        lockstep_lane_active_[i] = false;
    }
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
    stats_fetch_admitted_ = 0;
    stats_trace_missing_ = 0;
    seed_q_.clear();
    seed_queued_.clear();
    proven_not_main_.clear();
    broken_dep_q_.clear();
    broken_dep_seen_.clear();
    broken_dep_failed_.clear();
    uncle_q_.clear();
    uncle_queued_.clear();
    phase_ = Phase::BootstrapPoll;
    bootstrap_poll_done_ = false;
    trace_offset_ = 0;
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        lockstep_tip_height_[i] = -1;
        lockstep_lane_active_[i] = false;
    }
    scene_.clear_trace_edges();
    publish_trace_status_();
    if (fetch_pool_)
        fetch_pool_->reset_stats();
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
    phase_ = Phase::BootstrapPoll;
    bootstrap_poll_done_ = false;
    trace_offset_ = 0;
    publish_trace_status_();
    std::printf("[adapter] on_start phase=BootstrapPoll (poll gated after first lookback)\n");
}

void AlephiumAdapter::refresh_lookback_floors_()
{
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    // Base window: one lookback swath behind the network tip.
    // Camera can unlock further swaths (see camera_extra_lookback_heights_).
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
    std::printf("[adapter] base lookback floors set (~%d heights, window=%ds); "
                "camera can unlock more\n",
                lookback_blocks, ALPH_LOOKBACK_WINDOW_SECONDS);
}

int AlephiumAdapter::camera_extra_lookback_heights_() const
{
    // Layout: z = -(height - origin) * meters_per_height with meters ≈ block seconds.
    // Camera starts near -LOOKBACK_WINDOW seconds; scrolling toward older content
    // increases scroll_z (toward 0 / positive). Each full lookback period of Z
    // unlocks another swath of older heights.
    const float z = scene_.camera_scroll_z();
    const float z0 = initial_camera_scroll_z_;
    const float period_z = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS);
    if (period_z < 1.f)
        return 0;
    // older_delta > 0 when camera has moved toward older (higher world Z than start).
    const float older_delta = z - z0;
    if (older_delta < period_z)
        return 0;
    const int periods = static_cast<int>(older_delta / period_z);
    const int lookback_blocks =
        std::max(1, ALPH_LOOKBACK_WINDOW_SECONDS / ALPH_TARGET_BLOCK_SECONDS);
    return periods * lookback_blocks;
}

int AlephiumAdapter::effective_lookback_floor_(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return 0;
    if (!lookback_floors_valid_)
        return 0;
    const int extra = camera_extra_lookback_heights_();
    return std::max(0, min_lookback_height_[lane] - extra);
}

bool AlephiumAdapter::height_in_lookback_(uint32_t lane, int height) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return false;
    if (height < 0)
        return true;
    if (!lookback_floors_valid_)
        return true;
    return height >= effective_lookback_floor_(lane);
}

bool AlephiumAdapter::tip_pending_confirmation_() const
{
    for (const NodeId& tip : scene_.tip_ids())
    {
        auto n = scene_.graph().get(tip);
        if (!n)
            continue;
        if (!height_in_lookback_(n->lane, static_cast<int>(n->height)))
            continue;
        if (!main_chain_cache_.is_cached_main(tip))
            return true;
    }
    return false;
}

bool AlephiumAdapter::ready_for_poll() const
{
    // Bootstrap: allow exactly one lookback poll.
    if (phase_ == Phase::BootstrapPoll)
        return !bootstrap_poll_done_;
    // Gate all further window polls until lockstep confirms the initial pool.
    if (phase_ == Phase::IdentifyTips || phase_ == Phase::LockstepTrace)
        return false;
    // Steady: free interval polls.
    return phase_ == Phase::Steady;
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
    // Same-chain main trace over the *live pool* (graph + detail), never the
    // render/cull set. No HTTP: missing pool deps stay missing (orange in presenter).
    if (main_hash.empty() || budget <= 0)
        return 0;

    auto root = scene_.graph().get(main_hash);
    if (!root)
        return 0;

    const uint32_t chain_lane = root->lane;
    const int chain_from = static_cast<int>(root->group_from);
    const int chain_to = static_cast<int>(root->group_to);
    const int floor_h = effective_lookback_floor_(chain_lane);
    const int earliest = earliest_traced_height_[chain_lane];

    std::queue<std::string> q;
    std::unordered_set<std::string> seen;
    q.push(main_hash);
    seen.insert(main_hash);
    int marked = 0;
    int lowest_this_run = INT_MAX;

    while (!q.empty() && marked < budget)
    {
        const std::string cur = std::move(q.front());
        q.pop();

        auto node = scene_.graph().get(cur);
        if (!node)
            continue;

        if (node->lane != chain_lane ||
            static_cast<int>(node->group_from) != chain_from ||
            static_cast<int>(node->group_to) != chain_to)
            continue;

        const int from = static_cast<int>(node->group_from);
        const int to = static_cast<int>(node->group_to);
        const int height = static_cast<int>(node->height);

        // Outside unlocked lookback window — stop this branch.
        if (height >= 0 && height < floor_h)
            continue;

        const bool already = main_chain_cache_.is_cached_main(cur);
        if (!already)
        {
            main_chain_cache_.mark_main(cur);
            mark_scene_confirmed_(cur, from, to, height);
            ++marked;
            ++stats_dag_floods_;
        }
        if (height >= 0 && height < lowest_this_run)
            lowest_this_run = height;

        // Already-confirmed intermediate nodes: only expand unconfirmed children
        // that are newly unlocked by camera lookback (below previous earliest).
        // Fully inside previously traced band → terminate (no dep walk).
        if (already && cur != main_hash && earliest != INT_MAX && height >= earliest)
            continue;

        auto detail = scene_.detail_store().get(cur);
        if (!detail)
            continue;

        // Flood only walks same-chain deps already in the live pool.
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty() || !seen.insert(dep).second)
                continue;

            auto dn = scene_.graph().get(dep);
            if (!dn)
                continue; // missing from pool — no fetch; presenter paints orange

            if (dn->lane != chain_lane ||
                static_cast<int>(dn->group_from) != chain_from ||
                static_cast<int>(dn->group_to) != chain_to)
                continue;
            const int dh = static_cast<int>(dn->height);
            if (dh >= 0 && dh < floor_h)
                continue; // past beginning of unlocked window — terminate
            if (main_chain_cache_.is_cached_main(dep) &&
                earliest != INT_MAX && dh >= earliest)
                continue;
            q.push(dep);
        }
    }

    // Record earliest height labeled this run (terminate point for next flood).
    if (lowest_this_run != INT_MAX)
    {
        if (earliest_traced_height_[chain_lane] == INT_MAX ||
            lowest_this_run < earliest_traced_height_[chain_lane])
            earliest_traced_height_[chain_lane] = lowest_this_run;
    }
    return marked;
}

bool AlephiumAdapter::lane_needs_reflood_(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return false;
    // Never traced on this lane, or camera unlocked below previous earliest.
    if (earliest_traced_height_[lane] == INT_MAX)
        return true;
    return earliest_traced_height_[lane] > effective_lookback_floor_(lane);
}

int AlephiumAdapter::maybe_flood_offline_(const std::string& main_hash, uint32_t lane,
                                          int height, bool force)
{
    if (main_hash.empty())
        return 0;
    if (!force)
    {
        // Skip pure re-walk: lane already traced to floor and tip height already covered.
        const int ch = scene_.confirmed_height(lane);
        if (!lane_needs_reflood_(lane) && height >= 0 && ch >= height)
            return 0;
    }
    return flood_confirm_deps_offline_(main_hash, kMaxFloodPerSeed);
}

void AlephiumAdapter::label_tips_needing_reflood_()
{
    // Camera unlock only — lockstep owns bootstrap completeness.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        if (!lane_needs_reflood_(static_cast<uint32_t>(lane)))
            continue;
        const NodeId tip = scene_.confirmed_tip_hash(static_cast<uint32_t>(lane));
        if (tip.empty())
            continue;
        flood_confirm_deps_offline_(tip, kMaxFloodPerSeed);
    }
}

bool AlephiumAdapter::enqueue_missing_dep_(const std::string& dep_hash)
{
    if (dep_hash.empty())
        return false;
    if (scene_.graph().contains(dep_hash))
        return false;
    if (broken_dep_failed_.count(dep_hash))
        return false;
    if (fetch_pool_ && fetch_pool_->is_failed(dep_hash))
        return false;

    ++stats_trace_missing_;

    if (fetch_pool_)
        return fetch_pool_->enqueue(dep_hash);

    if (!broken_dep_seen_.insert(dep_hash).second)
        return false;
    if (fetch_and_admit_(dep_hash))
    {
        ++stats_fetch_admitted_;
        return true;
    }
    broken_dep_failed_.insert(dep_hash);
    return false;
}

void AlephiumAdapter::publish_trace_status_()
{
    scene_.set_trace_status(static_cast<int>(phase_), trace_offset_);
}

void AlephiumAdapter::set_phase_(Phase p)
{
    if (phase_ == p)
        return;
    phase_ = p;
    publish_trace_status_();
    const char* name = "Bootstrap";
    switch (p)
    {
    case Phase::BootstrapPoll: name = "BootstrapPoll"; break;
    case Phase::IdentifyTips:  name = "IdentifyTips";  break;
    case Phase::LockstepTrace: name = "LockstepTrace"; break;
    case Phase::Steady:        name = "Steady";        break;
    }
    std::printf("[adapter] phase → %s (offset=%d)\n", name, trace_offset_);
}

bool AlephiumAdapter::fetch_work_pending_() const
{
    if (!fetch_pool_)
        return false;
    return fetch_pool_->pending_jobs() > 0 || fetch_pool_->in_flight() > 0;
}

NodeId AlephiumAdapter::find_block_at_height_(uint32_t lane, int height) const
{
    if (height < 0)
        return {};
    // Prefer confirmed tip hash when it matches the target height.
    if (scene_.confirmed_height(lane) == height)
    {
        const NodeId tip = scene_.confirmed_tip_hash(lane);
        if (!tip.empty())
            return tip;
    }
    NodeId best;
    for (const GraphNode& n : scene_.graph().nodes_snapshot())
    {
        if (n.lane != lane || static_cast<int>(n.height) != height)
            continue;
        // Prefer main-cached / confirmed.
        if (main_chain_cache_.is_cached_main(n.id))
            return n.id;
        if (best.empty())
            best = n.id;
    }
    return best;
}

bool AlephiumAdapter::lockstep_lane_done_(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return true;
    if (!lockstep_lane_active_[lane])
        return true;
    const int tip_h = lockstep_tip_height_[lane];
    if (tip_h < 0)
        return true;
    const int target = tip_h - trace_offset_;
    const int floor_h = effective_lookback_floor_(lane);
    return target < floor_h;
}

bool AlephiumAdapter::lockstep_all_below_floor_() const
{
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        if (!lockstep_lane_active_[lane])
            continue;
        if (!lockstep_lane_done_(static_cast<uint32_t>(lane)))
            return false;
    }
    return true;
}

bool AlephiumAdapter::lockstep_step_complete_() const
{
    if (fetch_work_pending_())
        return false;

    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        if (!lockstep_lane_active_[lane])
            continue;
        if (lockstep_lane_done_(static_cast<uint32_t>(lane)))
            continue;

        const int tip_h = lockstep_tip_height_[lane];
        const int target = tip_h - trace_offset_;
        const NodeId block = find_block_at_height_(static_cast<uint32_t>(lane), target);
        if (block.empty())
            continue; // no live block at this height — treat as done for step

        // Must be confirmed (main path).
        if (!main_chain_cache_.is_cached_main(block))
            return false;

        auto detail = scene_.detail_store().get(block);
        if (!detail)
            return false;

        for (const std::string& dep : detail->deps)
        {
            if (dep.empty())
                continue;
            if (scene_.graph().contains(dep))
                continue;
            // Failed permanently: do not block lockstep forever.
            if (broken_dep_failed_.count(dep))
                continue;
            if (fetch_pool_ && fetch_pool_->is_failed(dep))
                continue;
            return false; // still missing and not failed
        }
    }
    return true;
}

void AlephiumAdapter::maybe_enter_lockstep_()
{
    if (phase_ != Phase::IdentifyTips)
        return;
    if (tip_pending_confirmation_())
        return;
    if (!seed_q_.empty())
        return;

    // Snapshot tip heights for lockstep (offset from each lane's tip).
    bool any = false;
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        lockstep_lane_active_[lane] = false;
        lockstep_tip_height_[lane] = -1;
        const NodeId tip = scene_.confirmed_tip_hash(static_cast<uint32_t>(lane));
        if (tip.empty())
            continue;
        auto n = scene_.graph().get(tip);
        const int h = n ? static_cast<int>(n->height) : scene_.confirmed_height(static_cast<uint32_t>(lane));
        if (h < 0)
            continue;
        lockstep_tip_height_[lane] = h;
        lockstep_lane_active_[lane] = true;
        any = true;
    }

    if (!any)
    {
        // No tips — go steady rather than hang.
        set_phase_(Phase::Steady);
        scene_.clear_trace_edges();
        return;
    }

    trace_offset_ = 0;
    set_phase_(Phase::LockstepTrace);
    std::printf("[adapter] lockstep start: all tips identified, offset=0\n");
    advance_lockstep_trace_();
}

void AlephiumAdapter::advance_lockstep_trace_()
{
    if (phase_ != Phase::LockstepTrace)
        return;

    // Admit fetches so current step can complete.
    drain_fetch_results_(kMaxFetchAdmitsPerDrain);

    if (lockstep_all_below_floor_())
    {
        std::printf("[adapter] lockstep complete → Steady (offset=%d)\n", trace_offset_);
        scene_.clear_trace_edges();
        set_phase_(Phase::Steady);
        return;
    }

    std::vector<TraceEdge> edges;
    edges.reserve(64);
    bool waiting_fetch = false;

    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        if (!lockstep_lane_active_[lane])
            continue;
        if (lockstep_lane_done_(static_cast<uint32_t>(lane)))
            continue;

        const int tip_h = lockstep_tip_height_[lane];
        const int target = tip_h - trace_offset_;
        NodeId block = find_block_at_height_(static_cast<uint32_t>(lane), target);
        if (block.empty())
            continue;

        auto node = scene_.graph().get(block);
        if (!node)
            continue;

        // Confirm main path at this height if tip flood already marked ancestors.
        if (main_chain_cache_.is_cached_main(block))
        {
            mark_scene_confirmed_(block, static_cast<int>(node->group_from),
                                  static_cast<int>(node->group_to), target);
        }
        else if (phase_ == Phase::LockstepTrace)
        {
            // Parent tip is main → offline-mark same-chain via tip flood once.
            const NodeId tip = scene_.confirmed_tip_hash(static_cast<uint32_t>(lane));
            if (!tip.empty())
                maybe_flood_offline_(tip, static_cast<uint32_t>(lane), tip_h, /*force=*/false);
            if (main_chain_cache_.is_cached_main(block))
                mark_scene_confirmed_(block, static_cast<int>(node->group_from),
                                      static_cast<int>(node->group_to), target);
        }

        auto detail = scene_.detail_store().get(block);
        if (!detail)
            continue;

        for (const std::string& dep : detail->deps)
        {
            if (dep.empty())
                continue;

            if (!scene_.graph().contains(dep))
            {
                if (!broken_dep_failed_.count(dep) &&
                    !(fetch_pool_ && fetch_pool_->is_failed(dep)))
                {
                    if (enqueue_missing_dep_(dep))
                        waiting_fetch = true;
                }
                continue;
            }

            // Animate listing → dep for this lockstep height.
            if (edges.size() < static_cast<size_t>(kMaxTraceEdges))
            {
                TraceEdge e;
                e.from = block;
                e.to = dep;
                edges.push_back(std::move(e));
            }
        }
    }

    scene_.set_trace_edges(std::move(edges));
    publish_trace_status_();

    if (waiting_fetch || fetch_work_pending_())
        return;

    if (!lockstep_step_complete_())
        return;

    // Advance one height back for all lanes together.
    ++trace_offset_;
    publish_trace_status_();
    std::printf("[adapter] lockstep advance → offset=%d\n", trace_offset_);

    if (lockstep_all_below_floor_())
    {
        std::printf("[adapter] lockstep complete → Steady (offset=%d)\n", trace_offset_);
        scene_.clear_trace_edges();
        set_phase_(Phase::Steady);
    }
}

void AlephiumAdapter::drain_fetch_results_(int max_admits)
{
    if (!fetch_pool_ || max_admits <= 0)
        return;

    auto results = fetch_pool_->drain_results(static_cast<size_t>(max_admits));
    for (auto& r : results)
    {
        if (!r.ok || r.body.empty())
        {
            broken_dep_failed_.insert(r.hash);
            continue;
        }

        cJSON* obj = cJSON_ParseWithLength(r.body.c_str(), r.body.size());
        if (!obj)
        {
            broken_dep_failed_.insert(r.hash);
            continue;
        }

        cJSON* block = unwrap_block_json(obj);
        AlphBlock alph(block);
        if (alph.hash.empty() || alph.hash != r.hash)
        {
            cJSON_Delete(obj);
            broken_dep_failed_.insert(r.hash);
            continue;
        }

        const bool added = scene_.add_block(block);
        cJSON_Delete(obj);

        if (added || scene_.graph().contains(r.hash))
            ++stats_fetch_admitted_;
    }
}

void AlephiumAdapter::enqueue_uncles_from_block_(const AlphBlock& alph)
{
    // Independent uncle queue — never mixed into tip seed work.
    for (const std::string& unc : alph.uncles)
    {
        if (unc.empty() || uncle_queued_.count(unc) || proven_not_main_.count(unc))
            continue;
        if (main_chain_cache_.is_cached_main(unc))
        {
            if (scene_.graph().contains(unc))
                mark_scene_confirmed_(unc);
            continue;
        }
        SeedJob uj;
        uj.hash = unc;
        uj.from = alph.chainFrom;
        uj.to = alph.chainTo;
        uj.height = alph.height; // parent height; refined when live
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

    // Already proven not-main: if it later appears in the live pool, remove it.
    if (proven_not_main_.count(uncle_hash))
    {
        if (scene_.graph().contains(uncle_hash))
        {
            std::printf("[adapter] uncle previously not-main now live %s — remove\n",
                        uncle_hash.c_str());
            scene_.remove_block(uncle_hash);
            ++stats_uncles_removed_;
            ++stats_removed_;
        }
        return;
    }

    ++stats_uncles_checked_;

    // Live pool only — no fetch. Not-main uncles in the pool are removed.
    auto node = scene_.graph().get(uncle_hash);
    const int from = node ? static_cast<int>(node->group_from) : parent_from;
    const int to = node ? static_cast<int>(node->group_to) : parent_to;
    const int height = node ? static_cast<int>(node->height) : parent_height;
    const uint32_t lane = node ? node->lane : lane_of(from, to);

    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();
    const int net_tip = main_chain_cache_.tip(from, to);
    const int frontier_h = scene_.confirmed_height(lane);
    const int tip_h = (frontier_h >= 0) ? frontier_h : net_tip;
    const bool behind_tip = (height >= 0 && tip_h >= 0 && height < tip_h);

    if (main_chain_cache_.is_cached_main(uncle_hash))
    {
        if (node)
            mark_scene_confirmed_(uncle_hash, from, to, height);
        return;
    }

    if (node && height >= 0 &&
        main_chain_cache_.try_hashes_singleton(uncle_hash, from, to, height))
    {
        mark_scene_confirmed_(uncle_hash, from, to, height);
        return;
    }

    bool transport_ok = false;
    ++stats_api_is_main_;
    if (main_chain_cache_.query_is_main(uncle_hash, &transport_ok))
    {
        if (node)
            mark_scene_confirmed_(uncle_hash, from, to, height);
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

    // Not on main chain.
    proven_not_main_.insert(uncle_hash);
    if (node)
    {
        // Remove from live pool (graph + detail), independent of render/cull.
        if (behind_tip)
            std::printf("[adapter] uncle orphaned (not main, h=%d < tip %d) %s — remove\n",
                        height, tip_h, uncle_hash.c_str());
        else
            std::printf("[adapter] uncle NOT main %s — remove from pool\n",
                        uncle_hash.c_str());
        scene_.remove_block(uncle_hash);
        ++stats_uncles_removed_;
        ++stats_removed_;
    }
    // Not in pool: remember not-main so a later admit is dropped immediately.
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
    const uint32_t mlane = lane_of(job.from, job.to);
    const int m = maybe_flood_offline_(main_hash, mlane, job.height, /*force=*/true);
    ++stats_replaced_;
    ++stats_verified_ok_;
    if (reselect)
        engine_.set_selection(main_hash);
    std::printf("[adapter] replaced with main %s [%d->%d] h=%d flood_marked=%d\n",
                main_hash.c_str(), job.from, job.to, job.height, m);
}

void AlephiumAdapter::confirm_seed_(const SeedJob& seed)
{
    // Tip-only verification — never walk the ancestor chain with is_main.
    const uint32_t lane = lane_of(seed.from, seed.to);

    if (main_chain_cache_.is_cached_main(seed.hash))
    {
        mark_scene_confirmed_(seed.hash, seed.from, seed.to, seed.height);
        // Already known main: only re-flood if lane incomplete / camera unlocked.
        // Completeness is lockstep during bootstrap (not full BFS here).
        maybe_flood_offline_(seed.hash, lane, seed.height, /*force=*/false);
        ++stats_verified_ok_;
        return;
    }

    if (!scene_.graph().contains(seed.hash))
        return; // nothing to do without a live tip in the pool

    if (!height_in_lookback_(lane, seed.height))
        return;

    if (seed.height >= 0 &&
        main_chain_cache_.try_hashes_singleton(seed.hash, seed.from, seed.to, seed.height))
    {
        mark_scene_confirmed_(seed.hash, seed.from, seed.to, seed.height);
        const int m = maybe_flood_offline_(seed.hash, lane, seed.height, /*force=*/true);
        ++stats_verified_ok_;
        if (m > 0)
            std::printf("[adapter] tip main (singleton) %s [%d->%d] h=%d — offline flood marked=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height, m);
        else
            std::printf("[adapter] tip main (singleton) %s [%d->%d] h=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height);
        return;
    }

    bool transport_ok = false;
    ++stats_api_is_main_;
    if (main_chain_cache_.query_is_main(seed.hash, &transport_ok))
    {
        mark_scene_confirmed_(seed.hash, seed.from, seed.to, seed.height);
        const int m = maybe_flood_offline_(seed.hash, lane, seed.height, /*force=*/true);
        ++stats_verified_ok_;
        if (m > 0)
            std::printf("[adapter] tip main %s [%d->%d] h=%d — offline flood marked=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height, m);
        else
            std::printf("[adapter] tip main %s [%d->%d] h=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height);
        return;
    }

    if (!transport_ok)
    {
        // Retry later (same tip only — do not expand to ancestors).
        enqueue_seed_(seed);
        return;
    }

    replace_non_main_(seed);
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
    drain_fetch_results_(kMaxFetchAdmitsPerDrain);

    const int uncle_budget = (max_jobs / 2) > 0 ? (max_jobs / 2) : 1;
    const int tip_budget = (max_jobs - uncle_budget) > 0 ? (max_jobs - uncle_budget) : 1;
    int n = 0;

    while (n < uncle_budget && running.load() && !uncle_q_.empty())
    {
        SeedJob uj = std::move(uncle_q_.front());
        uncle_q_.pop_front();
        verify_uncle_(uj.hash, uj.from, uj.to, uj.height);
        ++n;
    }

    // Tip identify (and Steady new tips): seed is_main work.
    if (phase_ == Phase::IdentifyTips || phase_ == Phase::Steady ||
        phase_ == Phase::BootstrapPoll)
    {
        if (seed_q_.empty() && tip_pending_confirmation_())
        {
            for (const NodeId& tip : scene_.tip_ids())
            {
                auto node = scene_.graph().get(tip);
                if (!node)
                    continue;
                if (!height_in_lookback_(node->lane, static_cast<int>(node->height)))
                    continue;
                if (main_chain_cache_.is_cached_main(tip))
                {
                    const int h = static_cast<int>(node->height);
                    mark_scene_confirmed_(tip, static_cast<int>(node->group_from),
                                          static_cast<int>(node->group_to), h);
                    maybe_flood_offline_(tip, node->lane, h, /*force=*/false);
                    continue;
                }
                SeedJob job;
                job.hash = tip;
                job.from = static_cast<int>(node->group_from);
                job.to = static_cast<int>(node->group_to);
                job.height = static_cast<int>(node->height);
                enqueue_seed_(std::move(job));
            }
        }

        int tip_n = 0;
        while (tip_n < tip_budget && running.load())
        {
            SeedJob job;
            if (!pop_seed_round_robin_(job))
                break;
            confirm_seed_(job);
            ++tip_n;
        }
    }

    if (phase_ == Phase::IdentifyTips)
        maybe_enter_lockstep_();
    else if (phase_ == Phase::LockstepTrace && running.load())
        advance_lockstep_trace_();

    if (phase_ == Phase::Steady)
        label_tips_needing_reflood_();

    drain_fetch_results_(kMaxFetchAdmitsPerDrain);
}

void AlephiumAdapter::poll_once(int64_t& last_poll_ts)
{
    maybe_refill_selection_detail();

    // Caller must gate on ready_for_poll(). Bootstrap = first lookback window only
    // until IdentifyTips + LockstepTrace complete.

    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    std::printf("\n[adapter] Polling blockflow from %lld to %lld (phase=%d)\n",
                static_cast<long long>(last_poll_ts), static_cast<long long>(now),
                static_cast<int>(phase_));

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

                // Previously proven not-main: never keep in the live pool.
                if (proven_not_main_.count(block_hash))
                {
                    if (scene_.graph().contains(block_hash))
                    {
                        scene_.remove_block(block_hash);
                        ++stats_uncles_removed_;
                        ++stats_removed_;
                    }
                    continue;
                }

                // Admit unconfirmed; confirmation is DAG work, not assume-main.
                // Live pool = graph + detail (not render/cull).
                const bool added_now = scene_.add_block(block);
                if (added_now)
                    ++added;

                // Queue ghost uncles for manual is_main (remove from pool if not main).
                {
                    AlphBlock alph(block);
                    if (!alph.hash.empty())
                        enqueue_uncles_from_block_(alph);
                }

                // Cached main: confirm in pool; flood only from tips (below) or new proves.
                if (main_chain_cache_.is_cached_main(block_hash))
                    mark_scene_confirmed_(block_hash, cf, ct, h);
                // Non-tips are labeled offline when a tip proves main — do not
                // enqueue every admit as an is_main seed (that re-traces history).
            }
        }
    }

    // Seed only current live tips for is_main (one API each).
    for (const NodeId& tip : scene_.tip_ids())
    {
        auto n = scene_.graph().get(tip);
        if (!n)
            continue;
        if (!height_in_lookback_(n->lane, static_cast<int>(n->height)))
            continue;
        if (main_chain_cache_.is_cached_main(tip))
        {
            const int h = static_cast<int>(n->height);
            mark_scene_confirmed_(tip, static_cast<int>(n->group_from),
                                  static_cast<int>(n->group_to), h);
            maybe_flood_offline_(tip, n->lane, h, /*force=*/false);
            continue;
        }
        SeedJob job;
        job.hash = tip;
        job.from = static_cast<int>(n->group_from);
        job.to = static_cast<int>(n->group_to);
        job.height = static_cast<int>(n->height);
        const size_t before = seed_q_.size();
        enqueue_seed_(std::move(job));
        if (seed_q_.size() > before)
            ++seeded;
    }

    drain_fetch_results_(kMaxFetchAdmitsPerDrain);

    const size_t fetch_pend = fetch_pool_ ? fetch_pool_->pending_jobs() : 0;
    const size_t fetch_inflight = fetch_pool_ ? fetch_pool_->in_flight() : 0;
    const int fetch_ok = fetch_pool_ ? fetch_pool_->stats_ok() : 0;
    const int fetch_fail = fetch_pool_ ? fetch_pool_->stats_fail() : 0;

    std::printf("[adapter] seen=%d added=%d seeds+=%d skipped_bad=%d "
                "seed_q=%zu uncle_q=%zu unconfirmed=%zu is_main_api=%d floods=%d "
                "uncles_checked=%d uncles_rm=%d confirmed_marks=%d removed=%d "
                "fetch_q=%zu inflight=%zu fetch_ok=%d fetch_fail=%d fetch_adm=%d "
                "trace_missing=%d phase=%d offset=%d\n",
                seen, added, seeded, skipped_bad, seed_q_.size(), uncle_q_.size(),
                scene_.unconfirmed_live_count(), stats_api_is_main_, stats_dag_floods_,
                stats_uncles_checked_, stats_uncles_removed_, stats_confirmed_marks_,
                stats_removed_, fetch_pend, fetch_inflight, fetch_ok, fetch_fail,
                stats_fetch_admitted_, stats_trace_missing_,
                static_cast<int>(phase_), trace_offset_);

    last_poll_ts = now;
    cJSON_Delete(obj);

    // After first bootstrap lookback poll: gate further polls until lockstep done.
    if (phase_ == Phase::BootstrapPoll)
    {
        bootstrap_poll_done_ = true;
        set_phase_(Phase::IdentifyTips);
    }

    prune_detail_store();
    maybe_refill_selection_detail();
}
