#include "network/pch.h"
#include "network/alephium/alephium_adapter.hpp"

#include <algorithm>
#include <cjson/cJSON.h>
#include <cstdarg>
#include <climits>
#include <cstdio>
#include <ctime>
#include <deque>
#include <queue>
#include <unordered_map>
#include <vector>

#include "domain/alph_block.hpp"
#include "network/commands.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
// Production: lifecycle/phase logs only. Set true for per-block / per-lane spam.
constexpr bool kAdapterVerbose = false;

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

// Rate-limited verbose logger (min interval between any verbose lines).
void adapter_vlog(const char* fmt, ...)
{
    if (!kAdapterVerbose)
        return;
    static double s_last = 0.0;
    const double now = static_cast<double>(std::time(nullptr));
    if (now - s_last < 0.25 && s_last > 0.0)
        return;
    s_last = now;
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
}
} // namespace

AlephiumAdapter::AlephiumAdapter(BlockScene& scene, IEngine& engine)
    : scene_(scene)
    , engine_(engine)
    , initial_camera_scroll_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
{
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        min_lookback_height_[i] = 0;
        earliest_traced_height_[i] = INT_MAX; // none traced yet
    }
    clear_bfs_state_();
    last_camera_extra_ms_ = 0;
    lookback_windows_.clear();
    genesis_resolved_ = false;
    genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    last_live_window_poll_ms_ = 0;
    last_cam_lookback_k_ = 0;
    live_poll_deferred_ = false;
    last_chunk_pump_ms_ = 0;
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
    pending_fill_parents_.clear();
    history_slots_fetched_.clear();
    phase_ = Phase::BootstrapPoll;
    bootstrap_poll_done_ = false;
    clear_bfs_state_();
    last_camera_extra_ms_ = 0;
    lookback_windows_.clear();
    genesis_resolved_ = false;
    genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    last_live_window_poll_ms_ = 0;
    last_cam_lookback_k_ = 0;
    live_poll_deferred_ = false;
    last_chunk_pump_ms_ = 0;
    last_segment_full_mask_ = 0;
    cache_pressure_level_ = 0;
    publish_trace_status_();
    if (fetch_pool_)
    {
        fetch_pool_->reset_stats();
        fetch_pool_->io().clear_completed_intervals();
    }
}

void AlephiumAdapter::full_reset()
{
    reset_stats();
    main_chain_cache_.clear();
    seed_lane_rr_ = 0;
    lookback_floors_valid_ = false;
    base_lookback_ms_ = 0;
    last_poll_wall_ms_ = 0;
    last_live_window_poll_ms_ = 0;
    last_cam_lookback_k_ = 0;
    live_poll_deferred_ = false;
    last_chunk_pump_ms_ = 0;
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        min_lookback_height_[i] = 0;
        earliest_traced_height_[i] = INT_MAX;
    }
}

void AlephiumAdapter::publish_hud(int domain, const char* base_url, bool switching)
{
    int status = 1; // Connecting
    switch (phase_)
    {
    case Phase::BootstrapPoll: status = 2; break; // Bootstrapping
    case Phase::IdentifyTips:  status = 3; break;
    case Phase::BfsTrace:      status = 4; break; // Confirm walk
    case Phase::Steady:        status = 5; break;
    }

    BlockScene::NetworkHud hud{};
    hud.domain = domain;
    hud.status = switching ? 6 : status;
    hud.switching = switching ? 1 : 0;
    if (base_url)
        std::snprintf(hud.base_url, sizeof(hud.base_url), "%.159s", base_url);

    int done = 0;
    for (const auto& w : lookback_windows_)
        if (w.polled)
            ++done;
    hud.lookback_windows_done = done;
    const int cam = camera_lookback_index_();
    hud.lookback_windows_need = std::max(1, cam + 1);
    if (static_cast<int>(lookback_windows_.size()) > hud.lookback_windows_need)
        hud.lookback_windows_need = static_cast<int>(lookback_windows_.size());

    int lanes_f = 0;
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
        if (scene_.frontier_valid(static_cast<uint32_t>(i)))
            ++lanes_f;
    hud.lanes_with_frontier = lanes_f;
    hud.open_confirm_walks = trace_offset();

    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            hud.tip_height_by_lane[f * ALPH_NUM_GROUPS + t] = main_chain_cache_.tip(f, t);

    hud.stats_api_is_main = stats_api_is_main_;
    hud.stats_fetch_admitted = stats_fetch_admitted_;
    hud.stats_removed = stats_removed_;
    hud.stats_seed_q = static_cast<int>(seed_q_.size());
    hud.last_poll_ms = last_poll_wall_ms_;
    hud.poll_interval_sec = static_cast<float>(cfg_.poll_interval_ms) * 0.001f;
    hud.cache_pressure_level = cache_pressure_level_;
    // History when camera lookback k>=1 (live tip window outside sliding 3-slot view).
    hud.browse_mode = (camera_lookback_index_() >= 1) ? 1 : 0;

    // Timeline segments: only the active triple-buffer ring (scrolling minimap).
    {
        update_segment_ring_();
        const auto nodes = scene_.nodes_snapshot();
        const bool fills_pending = !pending_fill_parents_.empty();
        int nseg = 0;
        for (int ri = 0; ri < active_ring_n_ && nseg < BlockScene::kMaxTimeSegments; ++ri)
        {
            const int wi = active_ring_[ri];
            if (wi < 0 || wi >= static_cast<int>(lookback_windows_.size()))
                continue;
            const LookbackWindowSlot& w = lookback_windows_[static_cast<size_t>(wi)];
            BlockScene::TimeSegment& s = hud.segments[nseg];
            s.index = w.index;
            s.global_index = w.global_index >= 0 ? w.global_index : lookback_to_global_(w.index);
            s.from_ms = w.from_ms;
            s.to_ms = w.to_ms;
            s.block_count = 0;
            for (const auto& n : nodes)
            {
                if (n.timestamp_ms <= 0)
                    continue;
                if (n.timestamp_ms >= w.from_ms && n.timestamp_ms < w.to_ms)
                    ++s.block_count;
            }
            const int64_t span_ms = std::max<int64_t>(1, w.to_ms - w.from_ms);
            const float span_sec = static_cast<float>(span_ms) * 0.001f;
            s.expected_blocks = std::max(
                1, static_cast<int>(span_sec / static_cast<float>(ALPH_TARGET_BLOCK_SECONDS) *
                                    static_cast<float>(ALPH_NUM_GROUPS * ALPH_NUM_GROUPS) * 0.5f));
            float density = static_cast<float>(s.block_count) /
                            static_cast<float>(std::max(1, s.expected_blocks));
            float chunk_prog = 0.f;
            if (w.chunks_total > 0)
                chunk_prog = static_cast<float>(w.chunks_done) /
                             static_cast<float>(w.chunks_total);
            else if (w.polled)
                chunk_prog = 1.f;
            float ratio = std::max(density * 0.5f + chunk_prog * 0.5f, chunk_prog * 0.85f);
            if (w.chunks_done > 0 || w.polled)
                ratio = std::max(ratio, 0.08f + 0.07f * chunk_prog);
            s.load_ratio = std::clamp(ratio, 0.f, 1.f);
            if (w.index == 0)
            {
                s.confirmed_full = 0;
                if (fills_pending)
                    s.load_ratio = std::min(s.load_ratio, 0.95f);
            }
            else
            {
                const bool dense_enough = s.block_count >= (s.expected_blocks * 85) / 100;
                s.confirmed_full =
                    (w.polled && dense_enough && !fills_pending) ? 1 : 0;
                if (s.confirmed_full)
                    s.load_ratio = 1.f;
                else if (fills_pending)
                    s.load_ratio = std::min(s.load_ratio, 0.95f);
            }
            ++nseg;
        }
        hud.segment_count = nseg;
    }

    scene_.set_network_hud(hud);
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash)
{
    if (hash.empty())
        return;
    auto n = scene_.graph().get(hash);
    if (n && n->lane < static_cast<uint32_t>(BlockScene::kLaneCount) && n->height >= 0)
    {
        mark_scene_confirmed_(hash, static_cast<int>(n->group_from),
                              static_cast<int>(n->group_to), static_cast<int>(n->height));
        return;
    }
    scene_.mark_confirmed(hash);
    ++stats_confirmed_marks_;
    const int nfill = enqueue_confirm_deps_(hash);
    if (nfill == 0)
        propagate_main_from_confirmed_deps_(hash);
}

void AlephiumAdapter::mark_scene_confirmed_(const std::string& hash, int from, int to, int height)
{
    if (hash.empty())
        return;
    const uint32_t lane = lane_of(from, to);
    const int hc = scene_.confirmed_height(lane);
    if (scene_.frontier_valid(lane) && height > hc + 1)
    {
        if (try_chain_walk_confirm_(hash, lane, height))
            return;
        // No clean path: bag-only confirm (no frontier jump).
        scene_.mark_confirmed(hash, lane, height, /*chain_walk=*/false);
        ++stats_confirmed_marks_;
        const int n = enqueue_confirm_deps_(hash);
        if (n == 0)
            propagate_main_from_confirmed_deps_(hash);
        return;
    }

    scene_.mark_confirmed(hash, lane, height, /*chain_walk=*/false);
    ++stats_confirmed_marks_;
    const int n = enqueue_confirm_deps_(hash);
    if (n == 0)
        propagate_main_from_confirmed_deps_(hash);
}

bool AlephiumAdapter::try_chain_walk_confirm_(const std::string& tip_hash, uint32_t lane,
                                              int height)
{
    // Walk tip --deps*--> current frontier tip (same lane H_c). Confirm path.
    const NodeId t_old = scene_.confirmed_tip_hash(lane);
    if (t_old.empty() || tip_hash == t_old)
        return false;
    if (!scene_.graph().contains(tip_hash) || !scene_.graph().contains(t_old))
        return false;

    std::unordered_map<std::string, std::string> parent; // child -> came-from (toward tip)
    std::deque<std::string> q;
    std::unordered_set<std::string> seen;
    q.push_back(tip_hash);
    seen.insert(tip_hash);
    bool found = false;
    constexpr int kMaxNodes = 512;
    int steps = 0;

    while (!q.empty() && steps < kMaxNodes)
    {
        const std::string cur = std::move(q.front());
        q.pop_front();
        ++steps;
        if (cur == t_old)
        {
            found = true;
            break;
        }
        auto detail = scene_.detail_store().get(cur);
        if (!detail)
            continue;
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty() || !scene_.graph().contains(dep))
                continue;
            if (!seen.insert(dep).second)
                continue;
            parent[dep] = cur; // dep is older; came from cur (newer)
            q.push_back(dep);
        }
    }
    if (!found)
        return false;

    // Reconstruct path from t_old toward tip: t_old, ..., tip_hash
    std::vector<std::string> path_old_to_new;
    {
        std::string cur = t_old;
        path_old_to_new.push_back(cur);
        // parent maps older -> newer? We stored parent[dep]=cur meaning dep is child of walk from cur
        // When expanding cur's deps, parent[dep]=cur means we reached dep from cur (tip-side).
        // From t_old, walk tip-ward: nodes that have parent[x]=t_old ... until tip
        // Actually reconstruction: start at t_old, follow reverse of parent?
        // We BFS from tip toward older. parent[older]=newer.
        // Path tipâ†’old: tip, ..., old. Reverse for oldâ†’tip.
        std::vector<std::string> tip_to_old;
        cur = t_old;
        tip_to_old.push_back(cur);
        while (cur != tip_hash)
        {
            auto it = parent.find(cur);
            if (it == parent.end())
            {
                // t_old was the start of BFS root? tip is root; parent only for nodes reached from tip.
                // t_old should have parent[t_old] = node that listed t_old as dep (newer).
                break;
            }
            cur = it->second;
            tip_to_old.push_back(cur);
            if (tip_to_old.size() > static_cast<size_t>(kMaxNodes))
                return false;
        }
        if (tip_to_old.back() != tip_hash)
            return false;
        path_old_to_new.assign(tip_to_old.rbegin(), tip_to_old.rend());
        // Wait: tip_to_old is t_old, next, ..., tip if we follow parent[older]=newer from t_old.
        // parent[dep]=cur means we expanded cur and found dep, so cur is closer to tip, dep is older.
        // So parent[older]=newer. From t_old walk parent repeatedly: t_old -> newer -> ... -> tip.
        // tip_to_old built as [t_old, newer, ..., tip] â€” that's already old_to_new!
        path_old_to_new = std::move(tip_to_old);
    }

    if (path_old_to_new.empty() || path_old_to_new.front() != t_old ||
        path_old_to_new.back() != tip_hash)
        return false;

    // Confirm intermediates tip-ward (skip t_old already confirmed).
    for (size_t i = 1; i < path_old_to_new.size(); ++i)
    {
        const std::string& h = path_old_to_new[i];
        auto node = scene_.graph().get(h);
        if (!node)
            return false;

        if (!main_chain_cache_.is_cached_main(h))
        {
            // Prefer free-main from path; else API.
            bool transport = false;
            ++stats_api_is_main_;
            if (!main_chain_cache_.query_is_main(h, &transport))
            {
                if (!transport)
                    return false;
                // Not main: cannot use this path (caller may replace tip separately).
                return false;
            }
        }

        const int nh = static_cast<int>(node->height);
        const uint32_t nl = node->lane;
        const bool same_lane = (nl == lane);
        scene_.mark_confirmed(h, nl, nh, /*chain_walk=*/same_lane && nh > scene_.confirmed_height(lane));
        ++stats_confirmed_marks_;
        enqueue_confirm_deps_(h);
    }

    scene_.set_frontier_walk(lane, path_old_to_new);
    propagate_main_from_confirmed_deps_(tip_hash);
    adapter_vlog("[adapter] chain-walk lane=%u steps=%zu tip=%s\n", lane,
                path_old_to_new.size(), tip_hash.c_str());
    return true;
}

void AlephiumAdapter::propagate_main_from_confirmed_deps_(const std::string& confirmed_hash)
{
    // Free main-chain advance: in-pool deps of a confirmed block are on-path without is_main.
    // Same-chain deps recurse (bounded); cross-shard one hop only.
    if (confirmed_hash.empty())
        return;

    std::vector<std::string> stack;
    std::unordered_set<std::string> seen;
    stack.push_back(confirmed_hash);
    seen.insert(confirmed_hash);
    int hops = 0;
    constexpr int kMaxFreeHops = 256;

    while (!stack.empty() && hops < kMaxFreeHops)
    {
        const std::string cur = std::move(stack.back());
        stack.pop_back();
        ++hops;

        auto detail = scene_.detail_store().get(cur);
        auto self = scene_.graph().get(cur);
        if (!detail || !self)
            continue;

        for (const std::string& dep : detail->deps)
        {
            if (dep.empty() || !scene_.graph().contains(dep))
                continue;
            auto dn = scene_.graph().get(dep);
            if (!dn)
                continue;

            const bool already = main_chain_cache_.is_cached_main(dep);
            if (!already)
                main_chain_cache_.mark_main(dep);
            scene_.mark_confirmed(dep, dn->lane, static_cast<int>(dn->height));
            if (!already)
                ++stats_confirmed_marks_;

            // Same-chain: keep walking free. Cross-shard: mark only (no further recurse).
            if (dn->lane == self->lane && seen.insert(dep).second)
                stack.push_back(dep);
        }
    }
}

void AlephiumAdapter::on_start()
{
    main_chain_cache_.refresh_tips();
    refresh_lookback_floors_();
    phase_ = Phase::BootstrapPoll;
    bootstrap_poll_done_ = false;
    clear_bfs_state_();
    last_camera_extra_ms_ = 0;
    lookback_windows_.clear();
    genesis_resolved_ = false;
    genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    last_segment_full_mask_ = 0;
    cache_pressure_level_ = 0;
    publish_trace_status_();
    std::printf("[adapter] on_start phase=BootstrapPoll (lookback window poll + time index)\n");
}

void AlephiumAdapter::refresh_lookback_floors_()
{
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    // Base window in time; height floors are approximate (DAG heights diverge).
    base_lookback_ms_ = cfg_.lookback_ms > 0
                            ? cfg_.lookback_ms
                            : static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    const int lookback_blocks =
        std::max(1, static_cast<int>(base_lookback_ms_ /
                                     (static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000)));

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
    std::printf("[adapter] lookback window=%lld ms (~%d heights/lane est); camera unlocks time\n",
                static_cast<long long>(base_lookback_ms_), lookback_blocks);
}

int64_t AlephiumAdapter::camera_extra_lookback_ms_() const
{
    // Must match camera_lookback_index_ (sticky origin + live tip Z).
    // Do NOT use construct-time initial_camera_scroll_z_: as wall clock advances,
    // live Z drifts more negative while z0 is fixed, so extra grew even at Live
    // and inflated height floors / graph admits far beyond the 3-slot ring.
    const float z = scene_.camera_scroll_z();
    const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    int64_t origin = scene_.timeline_origin_ms();
    const int64_t w = window_ms_();
    if (w <= 0)
        return 0;
    if (origin <= 0)
        origin = now_ms - w;
    const float live_z = -static_cast<float>(now_ms - origin) * 0.001f;
    const float older_delta_sec = z - live_z;
    int64_t eye_extra = 0;
    if (older_delta_sec >= 1.f)
        eye_extra = static_cast<int64_t>(older_delta_sec * 1000.f);
    // Floor covers the full view/fetch ring older edge (cam_k + 2), not just the
    // eye — otherwise height floors / time gates undershoot k+1/k+2.
    const int k = camera_lookback_index_();
    const int64_t ring_floor =
        static_cast<int64_t>(k + kSegmentRingSize) * w;
    int64_t extra = std::max(eye_extra, ring_floor);
    // Soft cap: ring floor already includes pad; allow mid-segment eye beyond it.
    const int64_t cap = static_cast<int64_t>(k + kSegmentRingSize + 1) * w;
    if (cap > 0 && extra > cap)
        extra = cap;
    // Never unlock past genesis wall-clock span.
    if (genesis_ms_ > 0 && now_ms > genesis_ms_)
    {
        const int64_t to_genesis = now_ms - genesis_ms_;
        if (extra > to_genesis)
            extra = to_genesis;
    }
    return extra;
}

int64_t AlephiumAdapter::lookback_start_ms_() const
{
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    const int64_t base = base_lookback_ms_ > 0
                             ? base_lookback_ms_
                             : static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    return now - base - camera_extra_lookback_ms_();
}

bool AlephiumAdapter::timestamp_in_lookback_(int64_t ts_ms) const
{
    if (ts_ms <= 0)
        return true; // unknown ts: do not cull by time
    return ts_ms >= lookback_start_ms_();
}

int AlephiumAdapter::effective_lookback_floor_(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return 0;
    if (!lookback_floors_valid_)
        return 0;
    // Approximate height floor from extra time (for height-only fallbacks).
    const int64_t extra_ms = camera_extra_lookback_ms_();
    const int extra_h =
        static_cast<int>(extra_ms /
                         (static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000));
    return std::max(0, min_lookback_height_[lane] - extra_h);
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

bool AlephiumAdapter::is_live_height_(uint32_t lane, int height) const
{
    // Base lookback floor only â€” camera-unlocked history is not "live chain".
    if (height < 0)
        return false;
    if (lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
        return false;
    if (!lookback_floors_valid_)
        return true;
    return height >= min_lookback_height_[lane];
}

bool AlephiumAdapter::is_live_block_(const std::string& hash) const
{
    if (hash.empty())
        return false;
    auto n = scene_.graph().get(hash);
    if (!n)
        return false;
    return is_live_height_(n->lane, static_cast<int>(n->height));
}

int64_t AlephiumAdapter::block_timestamp_ms_(const std::string& hash) const
{
    if (hash.empty())
        return 0;
    if (auto d = scene_.detail_store().get(hash))
    {
        if (d->timestamp > 0)
            return d->timestamp;
    }
    if (auto n = scene_.graph().get(hash))
    {
        if (n->timestamp_ms > 0)
            return n->timestamp_ms;
    }
    return 0;
}

int AlephiumAdapter::poll_time_slot_(int64_t from_ts, int64_t to_ts, bool force)
{
    if (from_ts < 0)
        from_ts = 0;
    if (to_ts <= from_ts)
        return 0;

    // Dedupe on exact chunk from_ts (windows are not always chunk-grid aligned).
    const int64_t slot_key = from_ts;
    if (!force && history_slots_fetched_.count(slot_key) != 0)
        return 0;

    adapter_vlog("[adapter] time-slot poll from=%lld to=%lld force=%d\n",
                static_cast<long long>(from_ts), static_cast<long long>(to_ts),
                force ? 1 : 0);

    // Prefer async interval GET via worker pool (overlaps RTT).
    if (fetch_pool_)
    {
        if (fetch_pool_->io().enqueue_interval(from_ts, to_ts, force))
            return 1; // enqueued; admit on drain_fetch_results_
        return 0;     // inflight / cap / queue full — retry later
    }

    // Sync fallback (no pool — tests / early init).
    cJSON* obj = get_blockflow_blocks_with_events(from_ts, to_ts);
    if (!obj)
        return 0;
    int seen = 0, added = 0;
    admit_blocks_with_events_(obj, &seen, &added);
    cJSON_Delete(obj);
    history_slots_fetched_.insert(slot_key);
    if (added > 0)
        adapter_vlog("[adapter] time-slot admit seen=%d added=%d\n", seen, added);
    return added > 0 ? added : 1;
}

int AlephiumAdapter::request_history_slot_for_block_(const std::string& hash)
{
    // Historical completeness: cover the block and one lookback of older deps.
    int64_t to_ts = block_timestamp_ms_(hash);
    if (to_ts <= 0)
    {
        auto n = scene_.graph().get(hash);
        if (!n || n->height < 0)
            return 0;
        const NodeId tip = scene_.confirmed_tip_hash(n->lane);
        int64_t tip_ts = block_timestamp_ms_(tip);
        if (tip_ts <= 0)
            tip_ts = static_cast<int64_t>(std::time(nullptr)) * 1000;
        auto tip_n = scene_.graph().get(tip);
        const int tip_h =
            tip_n ? static_cast<int>(tip_n->height) : static_cast<int>(n->height);
        const int dh = tip_h - static_cast<int>(n->height);
        to_ts = tip_ts - static_cast<int64_t>(dh) *
                             static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;
        if (to_ts < 0)
            to_ts = 0;
    }

    // One chunk around the block (not a full lookback window).
    const int64_t c = chunk_ms_();
    const int64_t from_ts = std::max<int64_t>(0, to_ts - c);
    const int64_t to_slack =
        to_ts + static_cast<int64_t>(ALPH_TARGET_BLOCK_SECONDS) * 1000;
    return poll_time_slot_(from_ts, to_slack);
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
    // Gate further window polls until per-chain DFS finishes.
    if (phase_ == Phase::IdentifyTips || phase_ == Phase::BfsTrace)
        return false;
    // Steady: no new discovery while confirmed blocks await dep fills (no gaps).
    if (phase_ == Phase::Steady && confirm_fills_pending_())
        return false;
    return phase_ == Phase::Steady;
}

int AlephiumAdapter::trace_offset() const
{
    // HUD: count of BFS threads still running.
    int n = 0;
    for (int i = 0; i < kBfsThreadCount; ++i)
        if (bfs_thr_[i].active && !bfs_thr_[i].done)
            ++n;
    return n;
}

void AlephiumAdapter::enqueue_seed_(SeedJob job)
{
    if (job.hash.empty())
        return;
    if (seed_queued_.count(job.hash) || proven_not_main_.count(job.hash))
        return;
    // Already confirmed â€” no work.
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

        // Outside unlocked lookback window â€” stop this branch.
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
        // Fully inside previously traced band â†’ terminate (no dep walk).
        if (already && cur != main_hash && earliest != INT_MAX && height >= earliest)
            continue;

        auto detail = scene_.detail_store().get(cur);
        if (!detail)
            continue;

        // Terminate BFS when any dependency cannot be found in the live pool.
        // Do not walk past a broken link (no further same-chain expansion).
        bool any_missing = false;
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty())
                continue;
            if (!scene_.graph().contains(dep))
            {
                any_missing = true;
                break;
            }
        }
        if (any_missing)
            continue; // stop this branch; do not enqueue children

        // All deps present â€” main path may expand same-chain deps already in pool.
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty() || !seen.insert(dep).second)
                continue;

            auto dn = scene_.graph().get(dep);
            if (!dn)
                continue;

            if (dn->lane != chain_lane ||
                static_cast<int>(dn->group_from) != chain_from ||
                static_cast<int>(dn->group_to) != chain_to)
                continue;
            const int dh = static_cast<int>(dn->height);
            if (dh >= 0 && dh < floor_h)
                continue; // past beginning of unlocked window â€” terminate
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
    // Camera unlock only â€” DFS owns bootstrap completeness.
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
    // Live-chain hash fetch only. Historical holes use time-slots.
    // Defer until live segment (window 0) has finished bulk fill in Steady,
    // so hash GETs do not compete with interval chunking on first load.
    if (dep_hash.empty())
        return false;
    if (scene_.graph().contains(dep_hash))
        return false;
    if (broken_dep_failed_.count(dep_hash))
        return false;
    if (fetch_pool_ && fetch_pool_->is_failed(dep_hash))
        return false;
    if (phase_ == Phase::Steady &&
        (lookback_windows_.empty() || !lookback_windows_[0].polled))
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

int AlephiumAdapter::enqueue_confirm_deps_(const std::string& parent_hash)
{
    // When a confirmed block has missing deps:
    //   live chain  â†’ per-hash fetch (build the live pool tightly)
    //   historical  â†’ time-slot interval poll (no hash crawl)
    if (parent_hash.empty())
        return 0;
    auto detail = scene_.detail_store().get(parent_hash);
    if (!detail)
        return 0;

    int missing = 0;
    for (const std::string& dep : detail->deps)
    {
        if (dep.empty())
            continue;
        if (scene_.graph().contains(dep))
            continue;
        ++missing;
    }
    if (missing == 0)
    {
        pending_fill_parents_.erase(parent_hash);
        return 0;
    }

    if (!is_live_block_(parent_hash))
    {
        // History: never hash-fetch from DFS/confirm. One time-based window.
        pending_fill_parents_.erase(parent_hash);
        const int added = request_history_slot_for_block_(parent_hash);
        adapter_vlog("[adapter] history hole parent=%s missing~=%d time-slot added=%d "
                    "(no hash fetch)\n",
                    parent_hash.c_str(), missing, added);
        return 0; // do not gate Steady on historical hash fills
    }

    int enqueued = 0;
    for (const std::string& dep : detail->deps)
    {
        if (dep.empty())
            continue;
        if (scene_.graph().contains(dep))
            continue;
        if (broken_dep_failed_.count(dep) ||
            (fetch_pool_ && fetch_pool_->is_failed(dep)))
            continue;
        if (enqueue_missing_dep_(dep))
        {
            ++enqueued;
            adapter_vlog("[adapter] live confirm fill dep parent=%s\n", parent_hash.c_str());
        }
        else if (!scene_.graph().contains(dep))
        {
            if (!(fetch_pool_ && fetch_pool_->is_failed(dep)) &&
                !broken_dep_failed_.count(dep))
                ++enqueued; // still outstanding (queue full / in flight)
        }
    }

    if (enqueued > 0)
    {
        pending_fill_parents_.insert(parent_hash);
        adapter_vlog("[adapter] live confirm fill pending parent=%s missing~=%d gate=1\n",
                    parent_hash.c_str(), enqueued);
    }
    else
    {
        pending_fill_parents_.erase(parent_hash);
    }
    return enqueued;
}

bool AlephiumAdapter::confirm_fills_pending_() const
{
    if (!pending_fill_parents_.empty())
        return true;
    if (fetch_pool_ &&
        (fetch_pool_->pending_jobs() > 0 || fetch_pool_->in_flight() > 0))
        return true;
    return false;
}

void AlephiumAdapter::recheck_confirm_fill_parents_()
{
    if (pending_fill_parents_.empty())
        return;

    std::vector<std::string> done;
    for (const std::string& parent : pending_fill_parents_)
    {
        // Historical parents should never sit in pending (no hash gate).
        if (!is_live_block_(parent))
        {
            done.push_back(parent);
            continue;
        }

        auto detail = scene_.detail_store().get(parent);
        if (!detail)
        {
            done.push_back(parent);
            continue;
        }
        bool still = false;
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty())
                continue;
            if (scene_.graph().contains(dep))
                continue;
            if (broken_dep_failed_.count(dep) ||
                (fetch_pool_ && fetch_pool_->is_failed(dep)))
                continue; // permanent hole â€” do not block forever
            still = true;
            enqueue_missing_dep_(dep); // live only
        }
        if (!still)
        {
            done.push_back(parent);
            propagate_main_from_confirmed_deps_(parent);
            adapter_vlog("[adapter] live confirm fill complete parent=%s\n", parent.c_str());
        }
    }
    for (const std::string& p : done)
        pending_fill_parents_.erase(p);

    // Resume BFS threads paused on a hole once fills progress.
    if (!done.empty())
    {
        for (int t = 0; t < kBfsThreadCount; ++t)
        {
            if (!bfs_thr_[t].paused || bfs_thr_[t].pause_hash.empty())
                continue;
            if (!scene_.graph().contains(bfs_thr_[t].pause_hash))
                continue;
            bfs_thr_[t].queue.push_back(bfs_thr_[t].pause_hash);
            bfs_thr_[t].pause_hash.clear();
            bfs_thr_[t].paused = false;
            bfs_thr_[t].done = false;
            bfs_thr_[t].active = true;
        }
    }
}

void AlephiumAdapter::publish_trace_status_()
{
    scene_.set_trace_status(static_cast<int>(phase_), trace_offset());
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
    case Phase::BfsTrace:      name = "BfsTrace";      break;
    case Phase::Steady:        name = "Steady";        break;
    }
    std::printf("[adapter] phase -> %s (bfs_threads_open=%d)\n", name, trace_offset());
}

void AlephiumAdapter::clear_bfs_state_()
{
    for (int t = 0; t < kBfsThreadCount; ++t)
    {
        bfs_thr_[t] = BfsThreadState{};
        bfs_thr_[t].done = true;
        bfs_thr_[t].active = false;
    }
    bfs_visited_.clear();
    bfs_seed_phase_ = BfsSeedPhase::None;
    // Keep generation sticky across clear only when caller bumps it.
    publish_bfs_traces_();
}

bool AlephiumAdapter::claim_bfs_visit_(const std::string& hash, int thread_id)
{
    if (hash.empty() || thread_id < 0 || thread_id >= kBfsThreadCount)
        return false;
    auto it = bfs_visited_.find(hash);
    if (it != bfs_visited_.end())
        return false;
    bfs_visited_.emplace(hash, thread_id);
    return true;
}

void AlephiumAdapter::push_bfs_edge_(int thread_id, const std::string& from,
                                    const std::string& to)
{
    if (thread_id < 0 || thread_id >= kBfsThreadCount)
        return;
    BfsThreadState& thr = bfs_thr_[thread_id];
    thr.edge_from.push_back(from);
    thr.edge_to.push_back(to);
    while (static_cast<int>(thr.edge_from.size()) > kBfsPathMaxEdges)
    {
        thr.edge_from.erase(thr.edge_from.begin());
        thr.edge_to.erase(thr.edge_to.begin());
    }
}

void AlephiumAdapter::publish_bfs_traces_()
{
    BlockScene::BfsTraceSnap snaps[kBfsThreadCount]{};
    for (int t = 0; t < kBfsThreadCount; ++t)
    {
        const BfsThreadState& thr = bfs_thr_[t];
        snaps[t].thread_id = t;
        snaps[t].generation = bfs_generation_;
        snaps[t].head = thr.head;
        if (!thr.active && thr.done && thr.edge_from.empty())
            snaps[t].active = 0;
        else if (thr.paused)
            snaps[t].active = 2;
        else if (thr.active && !thr.done)
            snaps[t].active = 1;
        else
            snaps[t].active = thr.edge_from.empty() ? 0 : 1;
        snaps[t].edge_from = thr.edge_from;
        snaps[t].edge_to = thr.edge_to;
    }
    scene_.set_bfs_traces(snaps, kBfsThreadCount);
}

bool AlephiumAdapter::bfs_threads_settled_() const
{
    for (int t = 0; t < kBfsThreadCount; ++t)
    {
        const BfsThreadState& thr = bfs_thr_[t];
        if (!thr.active)
            continue;
        if (!thr.done && !thr.queue.empty())
            return false;
        if (!thr.done && !thr.paused)
            return false;
    }
    return true;
}

bool AlephiumAdapter::all_bfs_done_() const
{
    for (int t = 0; t < kBfsThreadCount; ++t)
    {
        if (bfs_thr_[t].active && !bfs_thr_[t].done)
            return false;
    }
    return true;
}

void AlephiumAdapter::seed_bfs_phase_a_()
{
    // Only diagonal group tips [g->g].
    bfs_seed_phase_ = BfsSeedPhase::PhaseA;
    int seeded = 0;
    for (int g = 0; g < ALPH_NUM_GROUPS; ++g)
    {
        const uint32_t lane = static_cast<uint32_t>(g * ALPH_NUM_GROUPS + g);
        const NodeId tip = scene_.confirmed_tip_hash(lane);
        if (tip.empty())
            continue;
        const int t = g % kBfsThreadCount;
        if (!claim_bfs_visit_(tip, t))
            continue;
        BfsThreadState& thr = bfs_thr_[t];
        thr.queue.push_back(tip);
        thr.head = tip;
        thr.active = true;
        thr.done = false;
        thr.paused = false;
        thr.pause_hash.clear();
        ++seeded;
        adapter_vlog("[adapter] BFS phaseA seed t=%d lane=%d tip=%s\n", t,
                     static_cast<int>(lane), tip.c_str());
    }
    std::printf("[adapter] BFS phase A: seeded %d diagonal tips (N=%d threads)\n", seeded,
                kBfsThreadCount);
    publish_bfs_traces_();
}

void AlephiumAdapter::seed_bfs_phase_b_()
{
    // Remaining chain tips only if not already visited via phase A deps.
    bfs_seed_phase_ = BfsSeedPhase::PhaseB;
    int seeded = 0;
    int next_free = 0;
    for (int from = 0; from < ALPH_NUM_GROUPS; ++from)
    {
        for (int to = 0; to < ALPH_NUM_GROUPS; ++to)
        {
            if (from == to)
                continue; // diagonal already done in phase A
            const uint32_t lane = static_cast<uint32_t>(from * ALPH_NUM_GROUPS + to);
            const NodeId tip = scene_.confirmed_tip_hash(lane);
            if (tip.empty())
                continue;
            if (bfs_visited_.count(tip) != 0)
                continue; // mostly known — skip (no overdraw)

            int t = -1;
            for (int k = 0; k < kBfsThreadCount; ++k)
            {
                const int cand = (next_free + k) % kBfsThreadCount;
                if (bfs_thr_[cand].done || bfs_thr_[cand].queue.empty())
                {
                    t = cand;
                    next_free = (cand + 1) % kBfsThreadCount;
                    break;
                }
            }
            if (t < 0)
                t = static_cast<int>(lane) % kBfsThreadCount;
            if (!claim_bfs_visit_(tip, t))
                continue;
            BfsThreadState& thr = bfs_thr_[t];
            thr.queue.push_back(tip);
            thr.head = tip;
            thr.active = true;
            thr.done = false;
            thr.paused = false;
            thr.pause_hash.clear();
            ++seeded;
            adapter_vlog("[adapter] BFS phaseB seed t=%d lane=%d tip=%s\n", t,
                         static_cast<int>(lane), tip.c_str());
        }
    }
    if (seeded > 0)
        std::printf("[adapter] BFS phase B: seeded %d remaining unvisited tips\n", seeded);
    publish_bfs_traces_();
}

int AlephiumAdapter::expand_bfs_thread_(int thread_id, int node_budget)
{
    if (thread_id < 0 || thread_id >= kBfsThreadCount || node_budget <= 0)
        return 0;
    BfsThreadState& thr = bfs_thr_[thread_id];
    if (!thr.active || thr.done || thr.paused)
        return 0;

    int expanded = 0;
    while (!thr.queue.empty() && expanded < node_budget)
    {
        const std::string cur = thr.queue.front();
        thr.queue.pop_front();
        thr.head = cur;
        ++expanded;

        auto node = scene_.graph().get(cur);
        if (!node)
            continue;
        const uint32_t lane = node->lane;
        const int height = static_cast<int>(node->height);
        const int floor_h = effective_lookback_floor_(lane);
        if (height >= 0 && height < floor_h)
            continue;

        const int chain_from = static_cast<int>(node->group_from);
        const int chain_to = static_cast<int>(node->group_to);

        if (main_chain_cache_.is_cached_main(cur))
        {
            mark_scene_confirmed_(cur, chain_from, chain_to, height);
        }
        else
        {
            // Offline flood from confirmed tip of this lane when available.
            const NodeId tip = scene_.confirmed_tip_hash(lane);
            if (!tip.empty())
            {
                auto tip_n = scene_.graph().get(tip);
                if (tip_n)
                    maybe_flood_offline_(tip, lane, static_cast<int>(tip_n->height),
                                         /*force=*/false);
            }
            if (main_chain_cache_.is_cached_main(cur))
                mark_scene_confirmed_(cur, chain_from, chain_to, height);
        }

        auto detail = scene_.detail_store().get(cur);
        if (!detail)
            continue;

        bool any_missing = false;
        for (const std::string& dep : detail->deps)
        {
            if (dep.empty())
                continue;
            if (!scene_.graph().contains(dep))
            {
                any_missing = true;
                continue;
            }
            auto dn = scene_.graph().get(dep);
            if (!dn)
                continue;
            const int dh = static_cast<int>(dn->height);
            const int dep_floor = effective_lookback_floor_(dn->lane);
            if (dh >= 0 && dh < dep_floor)
                continue;
            // Cross-shard: claim once; owner thread paints the edge.
            if (claim_bfs_visit_(dep, thread_id))
            {
                thr.queue.push_back(dep);
                push_bfs_edge_(thread_id, cur, dep);
            }
        }

        if (any_missing)
        {
            thr.pause_hash = cur;
            thr.paused = true;
            thr.done = true;
            if (main_chain_cache_.is_cached_main(cur))
            {
                const bool live = is_live_height_(lane, height);
                const int n = enqueue_confirm_deps_(cur);
                adapter_vlog("[adapter] BFS pause t=%d h=%d live=%d fill~%d %s\n", thread_id,
                             height, live ? 1 : 0, n, cur.c_str());
            }
            else
            {
                adapter_vlog("[adapter] BFS end t=%d h=%d (missing dep, not main) %s\n",
                             thread_id, height, cur.c_str());
            }
            break;
        }
    }

    if (thr.queue.empty() && !thr.paused)
    {
        thr.done = true;
        thr.pause_hash.clear();
    }
    return expanded;
}

void AlephiumAdapter::maybe_restart_bfs_on_segment_()
{
    // Restart when any ring segment newly becomes confirmed_full.
    update_segment_ring_();
    int mask = 0;
    for (int ri = 0; ri < active_ring_n_; ++ri)
    {
        const int wi = active_ring_[ri];
        if (wi < 0 || wi >= static_cast<int>(lookback_windows_.size()))
            continue;
        const LookbackWindowSlot& w = lookback_windows_[static_cast<size_t>(wi)];
        // Approximate "full" with polled (presenter also uses density).
        if (w.polled)
            mask |= (1 << (wi & 31));
    }
    if (mask == 0 || mask == last_segment_full_mask_)
        return;
    // Only restart if new bits appeared (segment finished loading).
    if ((mask & ~last_segment_full_mask_) == 0)
    {
        last_segment_full_mask_ = mask;
        return;
    }
    last_segment_full_mask_ = mask;
    ++bfs_generation_;
    clear_bfs_state_();
    seed_bfs_phase_a_();
    std::printf("[adapter] BFS restart gen=%d (segment fully loaded / reconnect)\n",
                bfs_generation_);
}

void AlephiumAdapter::maybe_enter_bfs_()
{
    if (phase_ != Phase::IdentifyTips)
        return;
    if (tip_pending_confirmation_())
        return;
    if (!seed_q_.empty())
        return;

    // Need at least one diagonal tip.
    bool any = false;
    for (int g = 0; g < ALPH_NUM_GROUPS; ++g)
    {
        const uint32_t lane = static_cast<uint32_t>(g * ALPH_NUM_GROUPS + g);
        if (!scene_.confirmed_tip_hash(lane).empty())
        {
            any = true;
            break;
        }
    }
    if (!any)
    {
        set_phase_(Phase::Steady);
        return;
    }

    set_phase_(Phase::BfsTrace);
    ++bfs_generation_;
    clear_bfs_state_();
    seed_bfs_phase_a_();
    std::printf("[adapter] BFS confirm start: phase A diagonal tips only (pool-only expand)\n");
    advance_bfs_traces_();
}

void AlephiumAdapter::advance_bfs_traces_()
{
    if (phase_ != Phase::BfsTrace && phase_ != Phase::Steady)
        return;

    if (phase_ == Phase::Steady)
    {
        maybe_camera_history_extend_();
        maybe_restart_bfs_on_segment_();
    }

    // Budgeted parallel BFS: round-robin N logical threads.
    int budget = kMaxBfsNodesPerAdvance;
    int guard = kBfsThreadCount * 4;
    while (budget > 0 && guard-- > 0)
    {
        bool any = false;
        for (int t = 0; t < kBfsThreadCount && budget > 0; ++t)
        {
            if (!bfs_thr_[t].active || bfs_thr_[t].done || bfs_thr_[t].paused)
                continue;
            if (bfs_thr_[t].queue.empty())
            {
                bfs_thr_[t].done = true;
                continue;
            }
            const int slice = std::min(kMaxBfsNodesPerThreadSlice, budget);
            const int n = expand_bfs_thread_(t, slice);
            budget -= n;
            if (n > 0)
                any = true;
        }
        if (!any)
            break;
    }

    // Phase A settled -> phase B for unvisited non-diagonal tips only.
    if (bfs_seed_phase_ == BfsSeedPhase::PhaseA && bfs_threads_settled_())
        seed_bfs_phase_b_();

    publish_bfs_traces_();
    publish_trace_status_();

    if (phase_ == Phase::BfsTrace)
    {
        if (all_bfs_done_() && !confirm_fills_pending_() &&
            bfs_seed_phase_ == BfsSeedPhase::PhaseB)
        {
            std::printf("[adapter] BFS confirm complete -> Steady (fills clear)\n");
            set_phase_(Phase::Steady);
        }
        else if (all_bfs_done_() && !confirm_fills_pending_() &&
                 bfs_seed_phase_ == BfsSeedPhase::PhaseA)
        {
            // No phase B seeds; still enter Steady after A.
            seed_bfs_phase_b_();
            if (all_bfs_done_())
            {
                std::printf("[adapter] BFS confirm complete (A only) -> Steady\n");
                set_phase_(Phase::Steady);
            }
        }
        return;
    }

    // Steady: flood-label tips that BFS already covered / frontier stable.
    for (const NodeId& tip : scene_.tip_ids())
    {
        auto n = scene_.graph().get(tip);
        if (!n || !main_chain_cache_.is_cached_main(tip))
            continue;
        if (!height_in_lookback_(n->lane, static_cast<int>(n->height)))
            continue;
        const int h = static_cast<int>(n->height);
        mark_scene_confirmed_(tip, static_cast<int>(n->group_from),
                              static_cast<int>(n->group_to), h);
        const int ch = scene_.confirmed_height(n->lane);
        if (ch == h && bfs_visited_.count(tip) != 0)
            maybe_flood_offline_(tip, n->lane, h, /*force=*/false);
    }
}

int AlephiumAdapter::admit_blocks_with_events_(cJSON* obj, int* seen_out, int* added_out)
{
    int seen = 0, added = 0, skipped_bad = 0;
    if (!obj)
    {
        if (seen_out) *seen_out = 0;
        if (added_out) *added_out = 0;
        return 0;
    }

    GET_OBJECT_ITEM(obj, blocksAndEvents);
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
                const std::string block_hash = hash->valuestring;
                const int h = height->valueint;
                const int cf = chainFrom->valueint;
                const int ct = chainTo->valueint;

                if (proven_not_main_.count(block_hash))
                {
                    // Keep ghost uncles for visualization (distinct color); not confirm tips.
                    if (scene_.add_block(block))
                        ++added;
                    if (scene_.graph().contains(block_hash))
                        scene_.mark_uncle(block_hash);
                    {
                        AlphBlock alph(block);
                        if (!alph.hash.empty())
                            enqueue_uncles_from_block_(alph);
                    }
                    continue;
                }

                if (scene_.add_block(block))
                    ++added;

                {
                    AlphBlock alph(block);
                    if (!alph.hash.empty())
                        enqueue_uncles_from_block_(alph);
                }
                if (main_chain_cache_.is_cached_main(block_hash))
                    mark_scene_confirmed_(block_hash, cf, ct, h);
            }
        }
    }
    if (seen_out) *seen_out = seen;
    if (added_out) *added_out = added;
    (void)skipped_bad;
    return added;
}

void AlephiumAdapter::maybe_camera_history_extend_()
{
    // Only in Steady after live tip ready: unlock within triple-buffer runway.
    if (phase_ != Phase::Steady || !live_tip_pipeline_ready_())
        return;

    const int64_t extra_ms = camera_extra_lookback_ms_();
    if (extra_ms <= last_camera_extra_ms_)
        return;

    const int64_t window_ms =
        base_lookback_ms_ > 0
            ? base_lookback_ms_
            : static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    if (window_ms <= 0)
        return;

    const int old_periods = static_cast<int>(last_camera_extra_ms_ / window_ms);
    const int new_periods = static_cast<int>(extra_ms / window_ms);
    if (new_periods <= old_periods)
    {
        last_camera_extra_ms_ = extra_ms;
        return;
    }

    update_segment_ring_();
    for (int p = old_periods + 1; p <= new_periods; ++p)
    {
        if (!is_active_segment_(p))
            break;
        std::printf("[adapter] camera unlock history period=%d (ring)\n", p);
        ensure_lookback_window_(p, /*allow_live_poll=*/false);
    }

    last_camera_extra_ms_ = extra_ms;

    // Restart BFS from diagonal tips so newly unlocked history is traced.
    ++bfs_generation_;
    clear_bfs_state_();
    seed_bfs_phase_a_();
    std::printf("[adapter] BFS restart after camera unlock (extra_ms=%lld gen=%d)\n",
                static_cast<long long>(extra_ms), bfs_generation_);
}

int64_t AlephiumAdapter::window_ms_() const
{
    if (base_lookback_ms_ > 0)
        return base_lookback_ms_;
    return static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
}

int64_t AlephiumAdapter::chunk_ms_() const
{
    const int64_t w = window_ms_();
    if (w <= 0)
        return kTimelineChunkMs;
    return std::min(kTimelineChunkMs, w);
}

void AlephiumAdapter::resolve_genesis_ms_()
{
    if (genesis_resolved_)
        return;
    genesis_resolved_ = true;

    // Prefer height-0 block timestamp (chain 0â†’0).
    cJSON* arr = get_blockflow_hashes(0, 0, 0);
    if (arr && cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0)
    {
        cJSON* item = cJSON_GetArrayItem(arr, 0);
        if (item && cJSON_IsString(item) && item->valuestring)
        {
            const std::string hash = item->valuestring;
            cJSON* block_obj = get_blockflow_blocks_blockhash(hash.c_str());
            if (block_obj)
            {
                cJSON* block = unwrap_block_json(block_obj);
                AlphBlock alph(block);
                if (alph.timestamp > 0)
                {
                    genesis_ms_ = alph.timestamp;
                    scene_.set_genesis_ms(genesis_ms_);
                    std::printf("[adapter] genesis ts=%lld from height 0 %s\n",
                                static_cast<long long>(genesis_ms_), hash.c_str());
                    cJSON_Delete(block_obj);
                    cJSON_Delete(arr);
                    return;
                }
                cJSON_Delete(block_obj);
            }
        }
    }
    if (arr)
        cJSON_Delete(arr);

    genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    scene_.set_genesis_ms(genesis_ms_);
    std::printf("[adapter] genesis ts=%lld (fallback)\n",
                static_cast<long long>(genesis_ms_));
}

int AlephiumAdapter::max_lookback_index_() const
{
    return live_global_segment_id_(); // k max = G_live (lookback from live)
}

int AlephiumAdapter::live_global_segment_id_() const
{
    const int64_t w = window_ms_();
    if (w <= 0)
        return 0;
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    if (now <= genesis_ms_)
        return 0;
    return static_cast<int>((now - genesis_ms_) / w);
}

int AlephiumAdapter::global_segment_id_(int64_t ts_ms) const
{
    const int64_t w = window_ms_();
    if (w <= 0 || ts_ms <= genesis_ms_)
        return 0;
    const int G = static_cast<int>((ts_ms - genesis_ms_) / w);
    return std::max(0, std::min(G, live_global_segment_id_()));
}

int AlephiumAdapter::lookback_to_global_(int k) const
{
    const int G_live = live_global_segment_id_();
    return std::max(0, G_live - std::max(0, k));
}

int AlephiumAdapter::global_to_lookback_(int G) const
{
    const int G_live = live_global_segment_id_();
    if (G < 0)
        return G_live;
    return std::max(0, G_live - G);
}

void AlephiumAdapter::bounds_for_global_(int G, int64_t& from_ms, int64_t& to_ms) const
{
    const int64_t w = window_ms_();
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    const int G_live = live_global_segment_id_();
    G = std::clamp(G, 0, G_live);
    from_ms = genesis_ms_ + static_cast<int64_t>(G) * w;
    to_ms = from_ms + w;
    if (G == G_live && now < to_ms)
        to_ms = now; // live tip segment open-ended at now
    if (from_ms < genesis_ms_)
        from_ms = genesis_ms_;
    if (to_ms <= from_ms)
        to_ms = from_ms + 1;
}

int AlephiumAdapter::camera_lookback_index_() const
{
    // Index 0 = live window. Older camera Z (higher than live anchor) unlocks k>0.
    // scroll_z â‰ˆ live when attached; older_delta_sec â‰ˆ how far into the past.
    const float z = scene_.camera_scroll_z();
    const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    int64_t origin = scene_.timeline_origin_ms();
    if (origin <= 0)
        origin = now_ms - window_ms_();
    const float live_z = -static_cast<float>(now_ms - origin) * 0.001f;
    // Layout: older content has larger (less negative) Z. User scrolls +Z for history.
    const float older_delta_sec = z - live_z;
    if (older_delta_sec < 1.f)
        return 0;
    const float wsec = static_cast<float>(window_ms_()) * 0.001f;
    if (wsec < 1.f)
        return 0;
    const int k = static_cast<int>(older_delta_sec / wsec);
    return std::clamp(k, 0, max_lookback_index_());
}

void AlephiumAdapter::recompute_window_chunk_stats_(int window_index)
{
    if (window_index < 0 || window_index >= static_cast<int>(lookback_windows_.size()))
        return;
    LookbackWindowSlot& slot = lookback_windows_[static_cast<size_t>(window_index)];
    if (slot.to_ms <= slot.from_ms)
    {
        slot.chunks_total = 0;
        slot.chunks_done = 0;
        slot.polled = true;
        return;
    }
    const int64_t c = chunk_ms_();
    const int64_t span = slot.to_ms - slot.from_ms;
    slot.chunks_total = static_cast<int>((span + c - 1) / c);
    if (slot.chunks_total < 1)
        slot.chunks_total = 1;

    int done = 0;
    for (int64_t t = slot.to_ms; t > slot.from_ms;)
    {
        const int64_t chunk_to = t;
        const int64_t chunk_from = std::max(slot.from_ms, chunk_to - c);
        if (history_slots_fetched_.count(chunk_from) != 0)
            ++done;
        t = chunk_from;
        if (chunk_from <= slot.from_ms)
            break;
    }
    slot.chunks_done = done;
    slot.polled = (done >= slot.chunks_total && slot.chunks_total > 0 &&
                   slot.pending_from_ms == 0);

    // Only recompute cursor when nothing is in-flight (admit owns advances).
    if (slot.pending_from_ms > 0)
        return;

    int64_t next_to = slot.to_ms;
    for (int64_t t = slot.to_ms; t > slot.from_ms;)
    {
        const int64_t chunk_to = t;
        const int64_t chunk_from = std::max(slot.from_ms, chunk_to - c);
        if (history_slots_fetched_.count(chunk_from) == 0)
        {
            next_to = chunk_to;
            break;
        }
        t = chunk_from;
        next_to = chunk_from;
        if (chunk_from <= slot.from_ms)
            break;
    }
    slot.next_fill_to_ms = next_to;
}

void AlephiumAdapter::ensure_lookback_window_(int index, bool allow_live_poll)
{
    if (index < 0)
        return;
    const int max_i = max_lookback_index_();
    if (index > max_i)
        index = max_i;

    while (static_cast<int>(lookback_windows_.size()) <= index)
    {
        LookbackWindowSlot s;
        s.index = static_cast<int>(lookback_windows_.size());
        lookback_windows_.push_back(s);
    }

    LookbackWindowSlot& slot = lookback_windows_[static_cast<size_t>(index)];
    slot.index = index;
    const int G = lookback_to_global_(index);
    slot.global_index = G;

    // Genesis-aligned bounds: stable keys for planes/minimap/fetch.
    int64_t from_ms = 0, to_ms = 0;
    bounds_for_global_(G, from_ms, to_ms);

    if (index == 0 && !allow_live_poll)
    {
        if (slot.epoch_to_ms <= 0)
        {
            slot.from_ms = from_ms;
            slot.to_ms = to_ms;
            slot.epoch_to_ms = to_ms;
            slot.next_fill_to_ms = slot.to_ms;
            recompute_window_chunk_stats_(index);
        }
        return;
    }

    if (index == 0)
    {
        if (!allow_live_poll)
            return;
        const bool first = (slot.epoch_to_ms <= 0);
        if (first)
        {
            slot.from_ms = from_ms;
            slot.to_ms = to_ms;
            slot.epoch_to_ms = to_ms;
            slot.next_fill_to_ms = slot.to_ms;
            slot.pending_from_ms = 0;
            recompute_window_chunk_stats_(index);
            adapter_vlog("[adapter] live G=%d from=%lld to=%lld chunks=%d/%d\n", G,
                        static_cast<long long>(slot.from_ms),
                        static_cast<long long>(slot.to_ms), slot.chunks_done,
                        slot.chunks_total);
        }
        else if (slot.polled)
        {
            slot.want_newest_refresh = true;
            live_poll_deferred_ = false;
            // Extend live tip only (to = now); keep from = genesis-aligned.
            bounds_for_global_(G, from_ms, to_ms);
            slot.to_ms = to_ms;
            slot.global_index = G;
        }
        else
        {
            recompute_window_chunk_stats_(index);
        }
        return;
    }

    // History: freeze genesis-aligned bounds once.
    if (slot.epoch_to_ms <= 0)
    {
        slot.from_ms = from_ms;
        slot.to_ms = to_ms;
        slot.epoch_to_ms = to_ms;
        if (slot.to_ms <= slot.from_ms)
            return;
        slot.next_fill_to_ms = slot.to_ms;
        slot.pending_from_ms = 0;
        recompute_window_chunk_stats_(index);
        adapter_vlog("[adapter] history k=%d G=%d from=%lld to=%lld chunks=%d/%d\n",
                    index, G, static_cast<long long>(slot.from_ms),
                    static_cast<long long>(slot.to_ms), slot.chunks_done,
                    slot.chunks_total);
    }
    else
    {
        slot.global_index = G;
        recompute_window_chunk_stats_(index);
    }
}

bool AlephiumAdapter::fetch_window_chunk_(int window_index, bool force_newest)
{
    if (window_index < 0 ||
        window_index >= static_cast<int>(lookback_windows_.size()))
        return false;
    // Active ring, live tip force, or one-slot hysteresis prefetch beyond ring.
    const int cam_k = camera_lookback_index_();
    const int k_pref = effective_lookback_index_();
    const bool prefetch_beyond =
        !force_newest && k_pref > cam_k &&
        window_index == cam_k + kSegmentRingSize &&
        window_index <= max_lookback_index_();
    if (!is_active_segment_(window_index) && !(window_index == 0 && force_newest) &&
        !prefetch_beyond)
        return false;
    LookbackWindowSlot& slot = lookback_windows_[static_cast<size_t>(window_index)];
    if (slot.to_ms <= slot.from_ms)
        return false;

    // Wait for in-flight chunk admit before advancing (guaranteed progress).
    if (!force_newest && slot.pending_from_ms > 0)
        return false;

    const int64_t c = chunk_ms_();
    int64_t chunk_to = 0;
    int64_t chunk_from = 0;

    if (force_newest)
    {
        chunk_to = slot.to_ms;
        chunk_from = std::max(slot.from_ms, chunk_to - c);
    }
    else
    {
        if (slot.polled && !slot.want_newest_refresh)
            return false;
        int64_t fill_to = slot.next_fill_to_ms > 0 ? slot.next_fill_to_ms : slot.to_ms;
        if (fill_to <= slot.from_ms)
        {
            recompute_window_chunk_stats_(window_index);
            return false;
        }
        chunk_to = fill_to;
        chunk_from = std::max(slot.from_ms, chunk_to - c);
    }

    if (chunk_to <= chunk_from)
        return false;

    const bool force = force_newest;
    // Already admitted: advance cursor (no HTTP).
    if (!force && history_slots_fetched_.count(chunk_from) != 0)
    {
        slot.next_fill_to_ms = chunk_from;
        recompute_window_chunk_stats_(window_index);
        return false;
    }

    const int added = poll_time_slot_(chunk_from, chunk_to, force);
    if (added <= 0)
        return false;

    // Enqueued only — do NOT advance next_fill_to_ms until admit.
    if (!force_newest)
        slot.pending_from_ms = chunk_from;
    if (window_index == 0)
    {
        live_poll_deferred_ = false;
        if (force_newest)
            slot.want_newest_refresh = false;
    }

    adapter_vlog("[adapter] chunk enqueue win=%d [%lld,%lld) pending=%lld done=%d/%d\n",
                window_index, static_cast<long long>(chunk_from),
                static_cast<long long>(chunk_to),
                static_cast<long long>(slot.pending_from_ms), slot.chunks_done,
                slot.chunks_total);
    return true;
}

bool AlephiumAdapter::live_tip_pipeline_ready_() const
{
    // Deep history (≥2) only after Steady and live window fully chunk-filled.
    if (phase_ != Phase::Steady)
        return false;
    if (lookback_windows_.empty())
        return false;
    return lookback_windows_[0].polled;
}

bool AlephiumAdapter::dual_initial_complete_() const
{
    if (static_cast<int>(lookback_windows_.size()) < kInitialSegmentCount)
        return false;
    for (int i = 0; i < kInitialSegmentCount; ++i)
    {
        if (!lookback_windows_[static_cast<size_t>(i)].polled)
            return false;
        if (lookback_windows_[static_cast<size_t>(i)].pending_from_ms > 0)
            return false;
    }
    if (fetch_pool_ && fetch_pool_->io().inflight_intervals() > 0)
        return false;
    return true;
}

int AlephiumAdapter::effective_lookback_index_() const
{
    // Base camera lookback; bump early when eye is past mid of current segment
    // so k+1/k+2 start filling before the user fully crosses the plane.
    int k = camera_lookback_index_();
    const int max_k = max_lookback_index_();
    k = std::clamp(k, 0, max_k);
    if (!live_tip_pipeline_ready_())
        return k;

    const float z = scene_.camera_scroll_z();
    const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
    int64_t origin = scene_.timeline_origin_ms();
    if (origin <= 0)
        origin = now_ms - window_ms_();
    const float mps = 1.f;
    // Bounds for lookback window k (same as ensure genesis-aligned).
    int64_t from_ms = 0, to_ms = 0;
    bounds_for_global_(lookback_to_global_(k), from_ms, to_ms);
    if (to_ms <= from_ms)
        return k;
    const float z_from = -static_cast<float>(from_ms - origin) * 0.001f * mps;
    const float z_to = -static_cast<float>(to_ms - origin) * 0.001f * mps;
    const float z_new = std::min(z_from, z_to);
    const float z_old = std::max(z_from, z_to);
    const float span = z_old - z_new;
    if (span < 1.f)
        return k;
    // Progress 0 at newer edge → 1 at older edge.
    const float progress = std::clamp((z - z_new) / span, 0.f, 1.f);
    if (progress >= kPrefetchHysteresis && k < max_k)
        return k + 1;
    return k;
}

void AlephiumAdapter::update_segment_ring_()
{
    // View/fetch ring always follows camera: {cam_k, cam_k+1, cam_k+2}.
    // Do NOT freeze to {0,1,2} until Steady/tip-ready — that blocked history
    // past k2 during IdentifyTips/BfsTrace and left paging at k3+ empty.
    // live_tip_pipeline_ready_ still gates tip seeds / live poll / BFS only.
    // Do NOT base the ring on hysteresis-bumped k_eff — that drops live (k=0)
    // while the eye is still in N. Prefetch uses effective_lookback_index_ in pump.
    // Sorted older → newer for HUD/minimap (descending k).
    const int max_k = max_lookback_index_();
    int cam_k = camera_lookback_index_();
    cam_k = std::clamp(cam_k, 0, max_k);
    active_ring_n_ = 0;

    int tmp[3]{};
    int n = 0;
    for (int d = 0; d < kSegmentRingSize; ++d)
    {
        const int idx = cam_k + d;
        if (idx > max_k)
            break;
        tmp[n++] = idx;
    }
    // Sort descending k (older first).
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (tmp[j] > tmp[i])
                std::swap(tmp[i], tmp[j]);
    active_ring_n_ = n;
    for (int i = 0; i < n; ++i)
        active_ring_[i] = tmp[i];
}

bool AlephiumAdapter::is_active_segment_(int index) const
{
    for (int i = 0; i < active_ring_n_; ++i)
        if (active_ring_[i] == index)
            return true;
    return false;
}

void AlephiumAdapter::on_interval_chunk_admitted_(int64_t from_ms, int64_t to_ms)
{
    (void)to_ms;
    for (int wi = 0; wi < static_cast<int>(lookback_windows_.size()); ++wi)
    {
        LookbackWindowSlot& slot = lookback_windows_[static_cast<size_t>(wi)];
        if (slot.pending_from_ms == from_ms)
        {
            // Advance cursor past admitted chunk (newest-first: next end = from).
            slot.next_fill_to_ms = from_ms;
            slot.pending_from_ms = 0;
            slot.retry_count = 0;
        }
        recompute_window_chunk_stats_(wi);
    }
}

void AlephiumAdapter::on_interval_chunk_failed_(int64_t from_ms)
{
    for (int wi = 0; wi < static_cast<int>(lookback_windows_.size()); ++wi)
    {
        LookbackWindowSlot& slot = lookback_windows_[static_cast<size_t>(wi)];
        if (slot.pending_from_ms != from_ms)
            continue;
        slot.pending_from_ms = 0;
        ++slot.retry_count;
        if (slot.retry_count >= kChunkMaxRetries)
        {
            // Skip hole so progress continues; mark as fetched to avoid loop.
            history_slots_fetched_.insert(from_ms);
            slot.next_fill_to_ms = from_ms;
            slot.retry_count = 0;
            adapter_vlog("[adapter] chunk skip after retries from=%lld win=%d\n",
                        static_cast<long long>(from_ms), wi);
            recompute_window_chunk_stats_(wi);
        }
        // else leave cursor; next pump re-enqueues same chunk
    }
}

int AlephiumAdapter::pump_timeline_chunks_(int max_chunks)
{
    if (max_chunks <= 0)
        return 0;
    resolve_genesis_ms_();
    update_segment_ring_();
    const int cam_k = camera_lookback_index_();
    // Hysteresis only for fill priority / optional +1 beyond ring — not ring membership.
    const int k_pref = effective_lookback_index_();
    const bool live_cam = (cam_k == 0);
    const bool tip_ready = live_tip_pipeline_ready_();
    int fetched = 0;

    auto try_win = [&](int wi, bool force_newest) -> bool {
        if (fetched >= max_chunks)
            return false;
        // Allow force or active ring; also allow cam_k+3 when hysteresis wants early fill.
        if (!force_newest && !is_active_segment_(wi))
        {
            if (!(k_pref > cam_k && wi == cam_k + kSegmentRingSize && wi <= max_lookback_index_()))
                return false;
        }
        if (fetch_window_chunk_(wi, force_newest))
        {
            ++fetched;
            return true;
        }
        return false;
    };

    auto fill_win = [&](int wi) {
        if (wi < 0)
            return;
        if (wi >= static_cast<int>(lookback_windows_.size()))
            return;
        const bool in_ring = is_active_segment_(wi);
        const bool prefetch_beyond =
            k_pref > cam_k && wi == cam_k + kSegmentRingSize && wi <= max_lookback_index_();
        if (!in_ring && !prefetch_beyond)
            return;
        // Multiple enqueues per pump for ahead windows (pending blocks per-window).
        while (fetched < max_chunks && try_win(wi, false))
        {
        }
    };

    // Tip-backward load only (lookback k from live tip — never genesis-forward).
    // Live mode: k=0 first, then k=1, k=2.
    // History mode: camera window first, then older pad.
    if (live_cam && tip_ready && !lookback_windows_.empty() &&
        lookback_windows_[0].want_newest_refresh)
        try_win(0, /*force_newest=*/true);

    if (live_cam)
    {
        fill_win(0);
        fill_win(1);
        fill_win(2);
    }
    else
    {
        fill_win(cam_k);
        fill_win(cam_k + 1);
        fill_win(cam_k + 2);
    }
    // Optional early fill of one older beyond ring when deep in current segment.
    if (k_pref > cam_k)
        fill_win(cam_k + kSegmentRingSize);
    for (int i = 0; i < active_ring_n_ && fetched < max_chunks; ++i)
        fill_win(active_ring_[i]);

    if (fetched > 0)
        last_chunk_pump_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
    return fetched;
}

void AlephiumAdapter::ensure_windows_for_camera_()
{
    resolve_genesis_ms_();
    update_segment_ring_();
    const int k = camera_lookback_index_();
    if (k > 0)
        live_poll_deferred_ = true;
    // Active triple-buffer ring: camera segment + 2 older.
    for (int i = 0; i < active_ring_n_; ++i)
    {
        const int wi = active_ring_[i];
        if (wi == 0 && k > 0)
            continue; // no live rebuild while browsing history
        ensure_lookback_window_(wi, /*allow_live_poll=*/(k == 0));
    }
    // Hysteresis may pump cam_k+3 early — ensure bounds exist without adding to ring.
    const int k_pref = effective_lookback_index_();
    if (k_pref > k)
    {
        const int beyond = k + kSegmentRingSize;
        if (beyond <= max_lookback_index_())
            ensure_lookback_window_(beyond, /*allow_live_poll=*/false);
    }
}

void AlephiumAdapter::resync_live_chain_()
{
    // Force newest live chunk(s) + tip reseed; remaining window fills via pump.
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    std::printf("[adapter] live resync: force newest chunks + reseed tips (k=0)\n");
    ensure_lookback_window_(0, /*allow_live_poll=*/true);
    if (!lookback_windows_.empty())
    {
        lookback_windows_[0].want_newest_refresh = true;
        // Prefer re-filling any holes newest-first after force tip chunk.
        if (lookback_windows_[0].next_fill_to_ms <= 0)
            lookback_windows_[0].next_fill_to_ms = lookback_windows_[0].to_ms;
    }
    // Budget a few chunks now so tips exist before reseed.
    pump_timeline_chunks_(kMaxChunksPerPoll + 1);

    int seeded = 0;
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
    adapter_vlog("[adapter] live resync seeds+=%d seed_q=%zu now=%lld\n", seeded,
                seed_q_.size(), static_cast<long long>(now));
    if (phase_ == Phase::Steady || phase_ == Phase::IdentifyTips || phase_ == Phase::BfsTrace)
        advance_sequential_tips_();
}

void AlephiumAdapter::advance_sequential_tips_()
{
    // Request / mark next height H_c+1 only (one step per lane).
    if (!main_chain_cache_.tips_valid())
        main_chain_cache_.refresh_tips();

    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
        {
            const uint32_t lane = lane_of(f, t);
            const int net_tip = main_chain_cache_.tip(f, t);
            if (net_tip < 0)
                continue;

            const int hc = scene_.confirmed_height(lane);
            const bool have_frontier = scene_.frontier_valid(lane);
            if (have_frontier && net_tip <= hc)
            {
                scene_.clear_pending_tip(lane);
                continue;
            }

            // Prefer H_c+1; also try network tip for chain-walk when much ahead.
            std::vector<int> targets;
            if (!have_frontier)
                targets.push_back(net_tip);
            else
            {
                targets.push_back(hc + 1);
                if (net_tip > hc + 1)
                    targets.push_back(net_tip);
            }

            for (int target_h : targets)
            {
                cJSON* arr = get_blockflow_hashes(f, t, target_h);
                if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0)
                {
                    if (arr)
                        cJSON_Delete(arr);
                    continue;
                }

                std::string cand;
                if (cJSON_IsString(cJSON_GetArrayItem(arr, 0)) &&
                    cJSON_GetArrayItem(arr, 0)->valuestring)
                    cand = cJSON_GetArrayItem(arr, 0)->valuestring;
                cJSON_Delete(arr);
                if (cand.empty())
                    continue;

                if (!scene_.graph().contains(cand))
                {
                    if (fetch_and_admit_(cand))
                        ++stats_fetch_admitted_;
                }
                if (!scene_.graph().contains(cand))
                    continue;

                scene_.set_pending_tip(lane, cand);

                if (main_chain_cache_.is_cached_main(cand) ||
                    main_chain_cache_.try_hashes_singleton(cand, f, t, target_h))
                {
                    mark_scene_confirmed_(cand, f, t, target_h);
                    break;
                }

                SeedJob job;
                job.hash = cand;
                job.from = f;
                job.to = t;
                job.height = target_h;
                enqueue_seed_(std::move(job));
                break; // one pending seed per lane per tick
            }
        }
    }
}

void AlephiumAdapter::drain_fetch_results_(int max_admits)
{
    if (!fetch_pool_ || max_admits <= 0)
        return;

    auto results = fetch_pool_->drain_results(static_cast<size_t>(max_admits));
    for (auto& r : results)
    {
        using Kind = HttpIoPool::Kind;
        if (r.kind == Kind::Interval)
        {
            if (!r.ok || r.body.empty())
            {
                // HTTP fail: pool did not mark completed — retry allowed.
                on_interval_chunk_failed_(r.from_ms);
                continue;
            }
            cJSON* obj = cJSON_ParseWithLength(r.body.c_str(), r.body.size());
            if (!obj)
            {
                // HTTP ok but body unusable — forget completion so pump can retry.
                fetch_pool_->io().forget_completed_interval(r.from_ms);
                on_interval_chunk_failed_(r.from_ms);
                continue;
            }
            int seen = 0, added = 0;
            admit_blocks_with_events_(obj, &seen, &added);
            cJSON_Delete(obj);
            history_slots_fetched_.insert(r.from_ms);
            if (r.from_ms > 0 || r.to_ms > 0)
            {
                last_live_window_poll_ms_ =
                    static_cast<int64_t>(std::time(nullptr)) * 1000;
            }
            on_interval_chunk_admitted_(r.from_ms, r.to_ms);
            adapter_vlog("[adapter] interval admit from=%lld to=%lld seen=%d added=%d\n",
                        static_cast<long long>(r.from_ms),
                        static_cast<long long>(r.to_ms), seen, added);
            continue;
        }

        if (r.kind == Kind::IsMain)
        {
            // PR2: apply confirm on policy thread (body is JSON bool).
            continue;
        }

        // BlockHash
        if (!r.ok || r.body.empty())
        {
            if (!r.hash.empty())
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
        {
            ++stats_fetch_admitted_;
            if (main_chain_cache_.is_cached_main(r.hash))
                mark_scene_confirmed_(r.hash);
        }
    }
    recheck_confirm_fill_parents_();
}

void AlephiumAdapter::enqueue_uncles_from_block_(const AlphBlock& alph)
{
    // Independent uncle queue â€” never mixed into tip seed work.
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

    // Already proven not-main: keep in pool as uncle (viz), never as main tip.
    if (proven_not_main_.count(uncle_hash))
    {
        if (scene_.graph().contains(uncle_hash))
            scene_.mark_uncle(uncle_hash);
        return;
    }

    ++stats_uncles_checked_;

    // Live pool only â€” no fetch. Not-main uncles in the pool are removed.
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

    // Not on main chain — keep as uncle for viz (do not remove from pool).
    proven_not_main_.insert(uncle_hash);
    if (node)
    {
        if (behind_tip)
            adapter_vlog("[adapter] uncle orphaned (not main, h=%d < tip %d) %s — mark uncle\n",
                        height, tip_h, uncle_hash.c_str());
        else
            adapter_vlog("[adapter] uncle NOT main %s — mark uncle (keep draw)\n",
                        uncle_hash.c_str());
        scene_.mark_uncle(uncle_hash);
        // stats_uncles_removed_ kept as historical name; count "classified as uncle".
        ++stats_uncles_removed_;
    }
}

void AlephiumAdapter::replace_non_main_(const SeedJob& job)
{
    proven_not_main_.insert(job.hash);
    // Only keep as uncle if it was listed as a ghost uncle (queued) or already marked.
    if ((uncle_queued_.count(job.hash) || scene_.is_uncle(job.hash)) &&
        scene_.graph().contains(job.hash))
    {
        adapter_vlog("[adapter] DAG seed NOT main %s [%d->%d] h=%d — mark uncle\n",
                    job.hash.c_str(), job.from, job.to, job.height);
        scene_.mark_uncle(job.hash);
        ++stats_uncles_removed_;
        return;
    }
    adapter_vlog("[adapter] DAG seed NOT main %s [%d->%d] h=%d — remove\n",
                job.hash.c_str(), job.from, job.to, job.height);
    const bool reselect = engine_.is_selected(job.hash);
    scene_.remove_block(job.hash);
    if (reselect)
        engine_.clear_selection();
    ++stats_removed_;

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
    adapter_vlog("[adapter] replaced with main %s [%d->%d] h=%d flood_marked=%d\n",
                main_hash.c_str(), job.from, job.to, job.height, m);
}

void AlephiumAdapter::confirm_seed_(const SeedJob& seed)
{
    // Tip-only verification â€” never walk the ancestor chain with is_main.
    const uint32_t lane = lane_of(seed.from, seed.to);

    if (main_chain_cache_.is_cached_main(seed.hash))
    {
        mark_scene_confirmed_(seed.hash, seed.from, seed.to, seed.height);
        // Already known main: only re-flood if lane incomplete / camera unlocked.
        // Completeness via parallel BFS during BfsTrace / Steady.
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
            adapter_vlog("[adapter] tip main (singleton) %s [%d->%d] h=%d â€” offline flood marked=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height, m);
        else
            adapter_vlog("[adapter] tip main (singleton) %s [%d->%d] h=%d\n",
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
            adapter_vlog("[adapter] tip main %s [%d->%d] h=%d â€” offline flood marked=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height, m);
        else
            adapter_vlog("[adapter] tip main %s [%d->%d] h=%d\n",
                        seed.hash.c_str(), seed.from, seed.to, seed.height);
        return;
    }

    if (!transport_ok)
    {
        // Retry later (same tip only â€” do not expand to ancestors).
        enqueue_seed_(seed);
        return;
    }

    replace_non_main_(seed);
}

void AlephiumAdapter::maybe_memory_pressure_prune_()
{
    // Sliding ring is view/fetch only — do NOT drop graph nodes when the camera
    // leaves a segment. Blocks stay so revisiting redraws without re-GET.
    // Last-resort prune only under hard memory pressure (poller also enforces ~2 GB).
    // Soft pressure: set HUD warning only.
    if (phase_ != Phase::Steady)
        return;

    static constexpr size_t kSoftMemBytes = 1536ull * 1024 * 1024; // 1.5 GB
    static constexpr size_t kHardMemBytes = 2ull * 1024 * 1024 * 1024;
    static constexpr size_t kSoftMaxNodes = 200000;
    static constexpr size_t kHardMaxNodes = 250000;

    size_t private_bytes = 0;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc)))
        private_bytes = static_cast<size_t>(pmc.PrivateUsage);
#endif
    const size_t nodes = scene_.total_blocks();
    const bool soft =
        (private_bytes > 0 && private_bytes >= kSoftMemBytes) || nodes >= kSoftMaxNodes;
    const bool hard =
        (private_bytes > 0 && private_bytes >= kHardMemBytes) || nodes >= kHardMaxNodes;

    cache_pressure_level_ = hard ? 2 : (soft ? 1 : 0);
    if (cache_pressure_level_ == 0)
        return;

    static int64_t last_warn_ms = 0;
    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    if (now - last_warn_ms > 30000)
    {
        last_warn_ms = now;
        std::printf("[adapter] timeline cache pressure level=%d mem=%zu MB nodes=%zu "
                    "(retain all loaded segs until hard cap; future: disk cache)\n",
                    cache_pressure_level_, private_bytes / (1024 * 1024), nodes);
    }

    if (!hard)
        return;

    // Last resort: drop oldest nodes only (not ring-based discard).
    size_t cap = kHardMaxNodes * 9 / 10;
    if (private_bytes >= kHardMemBytes && nodes > 1000)
        cap = (std::min)(cap, nodes * 85 / 100);
    const size_t removed = scene_.prune(/*min_timestamp_ms=*/0, cap);
    if (removed > 0)
    {
        stats_removed_ += static_cast<int>(removed);
        std::printf("[adapter] memory pressure prune removed=%zu (cap=%zu) — "
                    "some history dropped; re-visit may re-fetch\n",
                    removed, cap);
    }
}

void AlephiumAdapter::prune_detail_store()
{
    const size_t slimmed = scene_.detail_store().prune_unpinned_txns();
    if (slimmed == 0)
        return;
    const DetailStoreStats st = scene_.detail_store().stats();
    adapter_vlog("[adapter] detail slim: pruned=%zu full=%zu slim=%zu total=%zu\n",
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
    // History / deep ring: prefer applying interval chunks so pending_from_ms clears.
    // Hash admits stay on the smaller budget so confirm work does not starve timeline.
    const int cam_k_early = camera_lookback_index_();
    const int interval_budget =
        (cam_k_early >= 1) ? kIntervalAdmitsPerDrain : kMaxFetchAdmitsPerDrain;
    drain_fetch_results_(std::max(interval_budget, kMaxFetchAdmitsPerDrain));
    recheck_confirm_fill_parents_();

    // History mode: cam_k >= 1 (live tip window k0 outside sliding view).
    // Live mode: cam_k == 0. Track continuously for resync + pump boost.
    const int cam_k = camera_lookback_index_();
    const int prev_k = last_cam_lookback_k_;
    last_cam_lookback_k_ = cam_k;
    const bool live_cam = (cam_k == 0); // Live in sliding window
    const bool history_mode = !live_cam;
    if (live_cam && prev_k > 0 && phase_ == Phase::Steady)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const int64_t poll_iv = cfg_.poll_interval_ms > 0 ? cfg_.poll_interval_ms : 8000;
        if (last_live_window_poll_ms_ == 0 || (now - last_live_window_poll_ms_) >= poll_iv)
            resync_live_chain_();
        else
            adapter_vlog("[adapter] return to live (drain): skip force poll (interval)\n");
    }
    else if (history_mode)
    {
        live_poll_deferred_ = true;
    }

    // Progressive timeline fill (rate-limited). Tip-backward: never genesis-first.
    // View/fetch ring follows cam_k even before tip pipeline is ready.
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const bool cam_stepped = (cam_k != prev_k);
        if (last_chunk_pump_ms_ == 0 || cam_stepped ||
            (now - last_chunk_pump_ms_) >= kChunkPumpIntervalMs)
        {
            ensure_windows_for_camera_();
            int budget = kAheadChunksPerDrain;
            if (phase_ == Phase::BootstrapPoll)
                budget = kBootstrapChunksPerDrain;
            else if (history_mode || cam_k >= 1)
                budget = std::max(kAheadChunksPerDrain, kMaxChunksPerPoll * 2);
            else if (!live_tip_pipeline_ready_())
                budget = std::max(kPreTipChunksPerDrain, kAheadChunksPerDrain);
            if (cam_stepped)
                budget = std::max(budget, kMaxChunksPerPoll * 2);
            pump_timeline_chunks_(budget);
            // Apply interval results immediately so next pump can advance cursors.
            if (history_mode || cam_stepped)
                drain_fetch_results_(kIntervalAdmitsPerDrain);
        }
    }

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

    // While fills pending: still allow is_main on already-live tips (IdentifyTips),
    // but Steady should not expand tip seeds until fills clear (no new discovery).
    const bool fills_block_steady_seeds =
        phase_ == Phase::Steady && confirm_fills_pending_();
    // History mode (live k0 outside sliding window): no new live tip discovery.

    if (phase_ == Phase::IdentifyTips || phase_ == Phase::BootstrapPoll ||
        (phase_ == Phase::Steady && !fills_block_steady_seeds && live_cam) ||
        (phase_ == Phase::BfsTrace && live_cam))
    {
        if (live_cam && seed_q_.empty() && tip_pending_confirmation_() &&
            !fills_block_steady_seeds)
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
            // IdentifyTips must finish is_main even if fills start mid-phase.
            if (fills_block_steady_seeds)
                break;
            // History mode: do not confirm new live tip seeds (drain only if any left).
            if (history_mode && phase_ == Phase::Steady)
                break;
            SeedJob job;
            if (!pop_seed_round_robin_(job))
                break;
            confirm_seed_(job);
            ++tip_n;
        }
    }

    if (phase_ == Phase::IdentifyTips)
    {
        if (live_cam)
            advance_sequential_tips_();
        maybe_enter_bfs_();
    }
    else if (phase_ == Phase::BfsTrace && running.load())
    {
        if (live_cam)
            advance_sequential_tips_();
        advance_bfs_traces_();
    }
    else if (phase_ == Phase::Steady && running.load())
    {
        if (live_cam)
        {
            advance_sequential_tips_();
            label_tips_needing_reflood_();
        }
        advance_bfs_traces_();
    }

    drain_fetch_results_(kMaxFetchAdmitsPerDrain);
    recheck_confirm_fill_parents_();
}

void AlephiumAdapter::poll_once(int64_t& last_poll_ts)
{
    maybe_refill_selection_detail();

    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    last_poll_wall_ms_ = now;
    ++poll_count_;
    if (poll_count_ == 1 || (poll_count_ % kTipRefreshEveryNPolls) == 0)
    {
        main_chain_cache_.refresh_tips();
        refresh_lookback_floors_();
    }

    resolve_genesis_ms_();
    base_lookback_ms_ = cfg_.lookback_ms > 0
                            ? cfg_.lookback_ms
                            : static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;

    const int cam_k = camera_lookback_index_();
    const int prev_k = last_cam_lookback_k_;
    last_cam_lookback_k_ = cam_k;
    const bool live_cam = (cam_k == 0);
    const bool returned_to_live = (prev_k > 0 && live_cam);
    const int64_t poll_iv = cfg_.poll_interval_ms > 0 ? cfg_.poll_interval_ms : 8000;
    const bool live_interval_elapsed =
        last_live_window_poll_ms_ == 0 ||
        (now - last_live_window_poll_ms_) >= poll_iv;

    // Bootstrap: high-budget fill of windows 0 then 1 (dual-segment) before tips.
    // Stay in Bootstrap until both complete so history ≥2 never races tip build.
    if (phase_ == Phase::BootstrapPoll)
    {
        std::printf("\n[adapter] Bootstrap: dual-segment fill (win 0 then 1, chunked)\n");
        ensure_lookback_window_(0, /*allow_live_poll=*/true);
        ensure_lookback_window_(1, /*allow_live_poll=*/false);
        pump_timeline_chunks_(kBootstrapChunksPerPoll);
        // Apply finished interval GETs so chunk progress / dual-complete can advance.
        drain_fetch_results_(64);

        if (dual_initial_complete_())
        {
            std::printf("[adapter] Bootstrap dual complete (seg0+seg1); IdentifyTips next\n");
            bootstrap_poll_done_ = true;
            set_phase_(Phase::IdentifyTips);
        }
        else
        {
            const int d0 = lookback_windows_.empty() ? 0 : lookback_windows_[0].chunks_done;
            const int t0 = lookback_windows_.empty() ? 0 : lookback_windows_[0].chunks_total;
            const int d1 = lookback_windows_.size() < 2 ? 0 : lookback_windows_[1].chunks_done;
            const int t1 = lookback_windows_.size() < 2 ? 0 : lookback_windows_[1].chunks_total;
            adapter_vlog("[adapter] Bootstrap progress win0=%d/%d win1=%d/%d\n", d0, t0, d1, t1);
        }
        last_poll_ts = now;
        maybe_refill_selection_detail();
        return;
    }

    std::printf("\n[adapter] Polling lookback windows (phase=%d now=%lld cam_k=%d mode=%s)\n",
                static_cast<int>(phase_), static_cast<long long>(now), cam_k,
                live_cam ? "Live" : "History");

    // History mode: live tip outside sliding window — halt live tip/API; only ring history.
    if (!live_cam)
    {
        live_poll_deferred_ = true;
        ensure_windows_for_camera_();
        // Tip-backward fill around camera only (cam_k..cam_k+2).
        pump_timeline_chunks_(std::max(kMaxChunksPerPoll, kMaxChunksPerPoll * 2));
        // High interval admit budget so multi-window history applies promptly.
        drain_fetch_results_(kHistoryIntervalAdmitsPerPoll);

        const size_t fetch_pend = fetch_pool_ ? fetch_pool_->pending_jobs() : 0;
        const size_t fetch_inflight = fetch_pool_ ? fetch_pool_->in_flight() : 0;
        adapter_vlog("[adapter] History mode cam_k=%d windows=%zu fetch_q=%zu "
                    "inflight=%zu (live tip halted)\n",
                    cam_k, lookback_windows_.size(), fetch_pend, fetch_inflight);

        last_poll_ts = now;
        prune_detail_store();
        maybe_memory_pressure_prune_();
        maybe_refill_selection_detail();
        return;
    }

    // Back at live tip: if away long enough (or never polled), live resync.
    if (returned_to_live && live_interval_elapsed)
    {
        resync_live_chain_();
        ensure_windows_for_camera_();
        pump_timeline_chunks_(kMaxChunksPerPoll);
    }
    else if (returned_to_live && !live_interval_elapsed)
    {
        adapter_vlog("[adapter] return to live: skip force poll (interval not elapsed)\n");
        ensure_windows_for_camera_();
        pump_timeline_chunks_(kMaxChunksPerPoll);
    }
    else
    {
        // Steady live: register window (newest-chunk refresh) + budgeted pump.
        ensure_lookback_window_(0, /*allow_live_poll=*/true);
        ensure_windows_for_camera_();
        pump_timeline_chunks_(kMaxChunksPerPoll);
    }

    // Seed current live tips for is_main (pool filled by lookback windows).
    // resync_live_chain_ already reseeded when that path ran.
    int seeded = 0;
    if (!(returned_to_live && live_interval_elapsed))
    {
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
    }

    drain_fetch_results_(kMaxFetchAdmitsPerDrain);

    const size_t fetch_pend = fetch_pool_ ? fetch_pool_->pending_jobs() : 0;
    const size_t fetch_inflight = fetch_pool_ ? fetch_pool_->in_flight() : 0;
    const int fetch_ok = fetch_pool_ ? fetch_pool_->stats_ok() : 0;
    const int fetch_fail = fetch_pool_ ? fetch_pool_->stats_fail() : 0;

    adapter_vlog("[adapter] windows=%zu cam_idx=%d seeds+=%d seed_q=%zu unconfirmed=%zu "
                "fetch_q=%zu inflight=%zu fetch_ok=%d fetch_fail=%d phase=%d\n",
                lookback_windows_.size(), cam_k, seeded,
                seed_q_.size(), scene_.unconfirmed_live_count(), fetch_pend,
                fetch_inflight, fetch_ok, fetch_fail, static_cast<int>(phase_));

    last_poll_ts = now;

    if (phase_ == Phase::Steady || phase_ == Phase::IdentifyTips || phase_ == Phase::BfsTrace)
        advance_sequential_tips_();

    prune_detail_store();
    maybe_memory_pressure_prune_();
    maybe_refill_selection_detail();
}
