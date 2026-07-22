#pragma once

// Domain/network policy for Alephium BlockFlow ingest.
// Living docs: docs/layers/network.md (presentation: docs/layers/app.md).
//
// Production keep list:
//   sequential H_c frontier, free-main dep propagation, chain-walk multi-step confirm,
//   pool-only parallel BFS confirm (BfsTrace), time-slot history, lookback windows.
// set_pending_tip = next-seed bookkeeping only (cyan Sobel is ScenePresenter-side).
//
// Phases: BootstrapPoll → IdentifyTips → BfsTrace (parallel BFS) → Steady.
// Live chain (base lookback window): per-hash fetches allowed to fill confirmed deps.
// Historical (below base floor / camera unlock): chunked time-slot interval polls only —
// no hash fetches from confirm walk. Free main for in-pool deps of confirmed blocks.
// Timeline GETs are 64s subsegments (newest-first, budgeted); Steady live refreshes open tip subseg.
// Presentation (solid/green/cyan/orange/gold) lives in ScenePresenter — not here.
#include "network/alephium/block_fetch_pool.hpp"
#include "network/alephium/main_chain_cache.hpp"
#include "network/alephium/segment_disk_cache.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AlephiumAdapter
{
public:
    // Bootstrap → identify tips → parallel BFS confirm → free polling.
    enum class Phase : int
    {
        BootstrapPoll = 0, // allow first lookback poll only
        IdentifyTips  = 1, // is_main all live tips; poll gated
        BfsTrace      = 2, // parallel BFS confirm (N=2G-1); poll gated
        Steady        = 3, // normal interval polls
    };
    // Alias for any external code still using the old name.
    static constexpr Phase DfsTrace = Phase::BfsTrace;

    struct Config
    {
        int64_t lookback_ms = 0;
        int64_t poll_interval_ms = 8000;
    };

    AlephiumAdapter(BlockScene& scene, IEngine& engine);

    void configure(const Config& cfg);
    void reset_stats();
    // Full internal reset for domain switch (queues, phase, caches); then call on_start().
    void full_reset();

    void set_fetch_pool(BlockFetchPool* pool) { fetch_pool_ = pool; }
    // Domain id for disk cache (mainnet/testnet); Debug disables cache.
    void set_disk_cache_domain(int domain);
    // Persist all windows with blocks (call on stop / domain switch / Esc path).
    void flush_disk_cache();

    void on_start();
    // Publish loading/activity HUD into BlockScene (call after poll/drain).
    void publish_hud(int domain, const char* base_url, bool switching = false);

    void poll_once(int64_t& last_poll_ts);

    // False during IdentifyTips / BfsTrace (and before first poll done).
    bool ready_for_poll() const;

    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    void maybe_refill_selection_detail();
    // Sole hard-prune owner (poller calls this). Invalidates chunk dedupe for dropped ranges.
    void maybe_memory_pressure_prune();

    Phase phase() const { return phase_; }
    // HUD: lanes still on confirm walk (0 when idle).
    int   trace_offset() const;

    size_t verify_queue_size() const { return seed_q_.size(); }
    int stats_verified_ok() const { return stats_verified_ok_; }
    int stats_removed() const { return stats_removed_; }
    int stats_replaced() const { return stats_replaced_; }
    int stats_detail_refilled() const { return stats_detail_refilled_; }
    int stats_confirmed_marks() const { return stats_confirmed_marks_; }
    int stats_dag_floods() const { return stats_dag_floods_; }
    int stats_api_is_main() const { return stats_api_is_main_; }
    int stats_uncles_checked() const { return stats_uncles_checked_; }
    int stats_uncles_removed() const { return stats_uncles_removed_; }
    int stats_fetch_admitted() const { return stats_fetch_admitted_; }
    int stats_trace_missing() const { return stats_trace_missing_; }

    int64_t poll_interval_ms() const { return cfg_.poll_interval_ms; }
    int64_t lookback_ms() const { return cfg_.lookback_ms; }

private:
    struct SeedJob
    {
        std::string hash;
        int from = 0;
        int to = 0;
        int height = 0;
    };

    void enqueue_seed_(SeedJob job);
    bool pop_seed_round_robin_(SeedJob& out);
    void confirm_seed_(const SeedJob& seed);
    // Same-chain offline mark from tip; does not walk past missing deps.
    int flood_confirm_deps_offline_(const std::string& main_hash, int budget);
    int maybe_flood_offline_(const std::string& main_hash, uint32_t lane, int height,
                             bool force);
    bool lane_needs_reflood_(uint32_t lane) const;
    bool fetch_and_admit_(const std::string& hash);
    void replace_non_main_(const SeedJob& job);
    void verify_uncle_(const std::string& uncle_hash, int parent_from, int parent_to,
                       int parent_height);
    void enqueue_uncles_from_block_(const AlphBlock& alph);
    void label_tips_needing_reflood_();
    void refresh_lookback_floors_();
    // Disk cache: whole G_seg units replace network interval fills.
    void bootstrap_disk_cache_();
    // Admit pending disk bootstrap blocks (budgeted) — avoids startup hitch.
    int  pump_bootstrap_admit_(int max_blocks);
    bool bootstrap_admit_pending_() const;
    void maybe_persist_verified_segments_(bool force = false);
    bool try_fill_window_from_disk_(int lookback_k);
    // Lazy-admit: schedule ring may be wider; body RAM only if |k - cam_k| ≤ admit half (k=0 always).
    bool lookback_k_in_admit_ring_(int lookback_k) const;
    // open_live_edge: leave topmost 64s subseg unfetched so network force-refreshes tip.
    void mark_window_complete_from_cache_(int lookback_k, int g_seg, int64_t from_ms,
                                          int64_t to_ms, bool open_live_edge = false);
    // Seed history_slots_fetched_ from disk presence (subset or all grid keys).
    void mark_window_present_chunks_from_cache_(int lookback_k, int g_seg, int64_t from_ms,
                                                int64_t to_ms, bool open_live_edge,
                                                const std::vector<int64_t>* present_keys,
                                                bool all_keys);
    // Dep-critical / eye timeline hole for priority interval patches.
    enum class HolePriority : uint8_t { Bulk = 0, Eye = 1, DepCritical = 2 };
    struct TimelineHole
    {
        int64_t      from_ms = 0;
        int64_t      to_ms = 0;
        HolePriority priority = HolePriority::Bulk;
        int          g_seg = -1;
        std::string  parent_hash;
    };
    void register_dep_hole_(const std::string& parent_hash);
    // DAG scan: collect broken edges; range if multi-missing, else single queue.
    struct BrokenDepStats
    {
        int64_t ts_min = 0;
        int64_t ts_max = 0;
        int     n_edges = 0;
        int     n_unique_missing = 0;
        std::vector<std::string> single_hashes; // when n_unique_missing==1
    };
    BrokenDepStats scan_broken_deps_(int node_budget);
    void maybe_enqueue_dag_range_(const BrokenDepStats& st);
    void enqueue_single_block_(const std::string& hash);
    int  pump_single_block_fetches_(int max_n);
    bool dep_critical_holes_pending_() const;
    bool pump_priority_holes_(int max_chunks);
    void mark_grid_keys_covered_(int64_t from_ms, int64_t to_ms);
    void set_disk_cache_event_(const char* fmt, ...);
    bool height_in_lookback_(uint32_t lane, int height) const;
    int  effective_lookback_floor_(uint32_t lane) const;
    // Extra older history unlocked by camera Z, in milliseconds (time, not heights).
    int64_t camera_extra_lookback_ms_() const;
    int64_t lookback_start_ms_() const;
    bool timestamp_in_lookback_(int64_t ts_ms) const;
    // True when height is inside the base live window (not camera-only history).
    bool is_live_height_(uint32_t lane, int height) const;
    bool is_live_block_(const std::string& hash) const;
    bool tip_pending_confirmation_() const;

    // Steady: request/confirm next height H_c+1 (adapter pending seed → green frontier).
    void advance_sequential_tips_();

    // Genesis timestamp + discrete lookback windows (index 0 = live tip window).
    void resolve_genesis_ms_();
    // Register / refresh window bounds; HTTP work is budgeted in pump_timeline_chunks_.
    // allow_live_poll: window 0 only scheduled when true (false while camera in history).
    void ensure_lookback_window_(int index, bool allow_live_poll = true);
    void ensure_windows_for_camera_();
    int  max_lookback_index_() const;
    int  camera_lookback_index_() const;
    int64_t window_ms_() const;
    int64_t chunk_ms_() const;
    // Global segment G from genesis: [genesis+G*W, genesis+(G+1)*W); live = highest G.
    int  live_global_segment_id_() const;
    int  global_segment_id_(int64_t ts_ms) const;
    int  lookback_to_global_(int k) const;
    int  global_to_lookback_(int G) const;
    void bounds_for_global_(int G, int64_t& from_ms, int64_t& to_ms) const;
    // Budgeted newest-first chunk GETs for needed windows. Returns chunks fetched.
    int  pump_timeline_chunks_(int max_chunks);
    // Fetch one chunk of a window (newest incomplete, or force newest for live).
    bool fetch_window_chunk_(int window_index, bool force_newest);
    void recompute_window_chunk_stats_(int window_index);
    // Force live newest chunk(s) + tip reseed after returning from history.
    void resync_live_chain_();
    // Enter/advance/exit history→live catch-up (fill missing 64s chunks first).
    void begin_live_catchup_();
    void pump_live_catchup_();
    bool live_catchup_ring_complete_() const;
    void finish_live_catchup_();
    // Steady + live window fully chunk-filled (gate deep history ≥2).
    bool live_tip_pipeline_ready_() const;
    // Dual-segment initial: windows 0 and 1 both polled (history hygiene; not bootstrap gate).
    bool dual_initial_complete_() const;
    // Live window tip-ready: enough to leave Bootstrap (does not require win1 complete).
    bool live_window_tip_ready_() const;
    // Genesis-aligned open live tip subsegment [from, from+chunk). History must stay strictly older.
    int64_t live_open_subseg_from_(int64_t now_ms) const;
    bool history_subseg_allowed_(int64_t chunk_from, int64_t chunk_to,
                                 int64_t live_open_from) const;
    // Triple-buffer ring of absolute lookback indices (≤ kSegmentRingSize).
    void update_segment_ring_();
    bool is_active_segment_(int index) const;
    // Lookback k with prefetch hysteresis (start k+1/k+2 before full cross).
    int  effective_lookback_index_() const;
    // On successful interval admit: advance fill cursor for owning window(s).
    void on_interval_chunk_admitted_(int64_t from_ms, int64_t to_ms);
    void on_interval_chunk_failed_(int64_t from_ms);
    // Throttle/backoff after 429/5xx (or repeated fails). Pauses interval pumps.
    void note_http_interval_outcome_(bool ok, long http_code);
    bool interval_pump_allowed_() const;

    // Apply fetch results with separate interval vs hash/is_main budgets.
    void drain_fetch_results_(int interval_budget, int other_budget);
    void drain_fetch_results_(int max_admits)
    {
        drain_fetch_results_(max_admits, max_admits);
    }
    // After hard prune: drop load-once keys for time ranges no longer in graph.
    void invalidate_interval_dedupe_before_(int64_t min_keep_ts_ms);
    // Live-chain only: per-hash GET /blocks/{hash}.
    bool enqueue_missing_dep_(const std::string& dep_hash);
    // On confirm: live → hash-fill deps; historical → time-slot poll (no hash).
    int  enqueue_confirm_deps_(const std::string& parent_hash);
    // Live parents still awaiting dep hash-fills (not interval/history jobs).
    bool confirm_fills_pending_() const;
    void recheck_confirm_fill_parents_();

    // Historical / bulk discovery: GET /blocks-with-events?fromTs&toTs.
    int  poll_time_slot_(int64_t from_ts, int64_t to_ts, bool force = false);
    // One lookback window ending at the block's timestamp (covers older deps).
    int  request_history_slot_for_block_(const std::string& hash);
    int64_t block_timestamp_ms_(const std::string& hash) const;

    // Phase / parallel BFS confirm (pool-only; live holes hash-fill, history uses time slots)
    void set_phase_(Phase p);
    void maybe_enter_bfs_();
    void advance_bfs_traces_();
    void clear_bfs_state_();
    void seed_bfs_phase_a_();
    void seed_bfs_phase_b_();
    bool bfs_threads_settled_() const;
    bool all_bfs_done_() const;
    bool claim_bfs_visit_(const std::string& hash, int thread_id);
    void push_bfs_edge_(int thread_id, const std::string& from, const std::string& to);
    int  expand_bfs_thread_(int thread_id, int node_budget);
    void publish_bfs_traces_();
    void maybe_restart_bfs_on_segment_();
    void maybe_camera_history_extend_();
    int  admit_blocks_with_events_(cJSON* obj, int* seen_out, int* added_out);
    void publish_trace_status_();

    void mark_scene_confirmed_(const std::string& hash);
    void mark_scene_confirmed_(const std::string& hash, int from, int to, int height);
    // Anchor green H_c to network tip (chain_walk jump allowed).
    void mark_scene_anchor_(const std::string& hash, int from, int to, int height);
    // Forward novelty: if ALL known deps are Main, mark B Main (no is_main).
    // Returns true if hash is Main after call (already was, or newly promoted).
    bool try_forward_promote_(const std::string& hash);
    // After promote/admit: re-try pending tips + confirm-fill parents.
    void after_main_or_admit_(const std::string& hash);
    // Mark in-pool deps of a confirmed block as main (no is_main API).
    void propagate_main_from_confirmed_deps_(const std::string& confirmed_hash);
    // If tip is > H_c+1, walk deps to current frontier; confirm path; set walk anim.
    bool try_chain_walk_confirm_(const std::string& tip_hash, uint32_t lane, int height);
    void prune_detail_store();
    // Internal implementation of maybe_memory_pressure_prune (public API).
    void maybe_memory_pressure_prune_();

    BlockScene& scene_;
    IEngine& engine_;
    Config cfg_{};
    MainChainCache main_chain_cache_;
    BlockFetchPool* fetch_pool_ = nullptr;

    std::deque<SeedJob> seed_q_;
    std::unordered_set<std::string> seed_queued_;
    std::unordered_set<std::string> proven_not_main_;
    std::deque<std::string> broken_dep_q_;
    std::unordered_set<std::string> broken_dep_seen_;
    std::unordered_set<std::string> broken_dep_failed_;
    // Confirmed parents waiting for missing deps to enter the pool.
    std::unordered_set<std::string> pending_fill_parents_;
    std::deque<SeedJob> uncle_q_;
    std::unordered_set<std::string> uncle_queued_;

    Phase phase_ = Phase::BootstrapPoll;
    bool bootstrap_poll_done_ = false;

    // Parallel BFS confirm: N = 2G-1 cooperative workers (not 16 DFS lanes).
    static constexpr int kBfsThreadCount = BlockScene::kBfsThreadCount;
    static constexpr int kBfsPathMaxEdges = BlockScene::kBfsTraceMaxEdges;
    static constexpr int kMaxBfsNodesPerAdvance = 256;
    enum class BfsSeedPhase : int { None = 0, PhaseA = 1, PhaseB = 2 };
    struct BfsThreadState
    {
        std::deque<std::string> queue;
        std::vector<std::string> edge_from;
        std::vector<std::string> edge_to;
        std::string pause_hash;
        std::string head;
        bool active = false;
        bool done = true;
        bool paused = false;
    };
    BfsThreadState bfs_thr_[kBfsThreadCount]{};
    std::unordered_map<std::string, int> bfs_visited_; // hash → owner thread
    BfsSeedPhase bfs_seed_phase_ = BfsSeedPhase::None;
    int bfs_generation_ = 0;
    // Track segment confirmed_full fingerprint for restart.
    int last_segment_full_mask_ = 0;
    int64_t last_camera_extra_ms_ = 0;

    int min_lookback_height_[BlockScene::kLaneCount]{};
    int earliest_traced_height_[BlockScene::kLaneCount]{};
    bool lookback_floors_valid_ = false;
    float initial_camera_scroll_z_ = 0.f;
    int64_t base_lookback_ms_ = 0;
    // Lookback windows: index 0 = [now-W, now], higher = older toward genesis.
    // HTTP fills are chunked (kTimelineChunkMs); polled = all chunks in span done.
    struct LookbackWindowSlot
    {
        int     index = 0;            // lookback k (0 = live)
        int     global_index = -1;    // G from genesis
        int64_t from_ms = 0;
        int64_t to_ms = 0;
        bool    polled = false;           // all chunks fetched once
        int     chunks_done = 0;
        int     chunks_total = 0;
        // Newest-first fill cursor: exclusive end of next chunk to request.
        // Advances only after successful admit (not enqueue).
        int64_t next_fill_to_ms = 0;
        // In-flight chunk from_ms (0 = none); blocks cursor until admit/fail.
        int64_t pending_from_ms = 0;
        int     retry_count = 0;
        // Frozen upper bound for this fill epoch (live window especially).
        int64_t epoch_to_ms = 0;
        // Live Steady: re-request newest chunk only (not full window).
        bool    want_newest_refresh = false;
    };
    std::vector<LookbackWindowSlot> lookback_windows_;
    // Active load/fetch ring (lookback k); size ≤ kSegmentRingSize (load ring).
    int active_ring_[ALPH_LOAD_RING_SEGMENTS]{};
    int active_ring_n_ = 0;
    int64_t genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    bool    genesis_resolved_ = false;
    // Live-chain poll gate: skip window-0 API while camera lookback k > 0; on return
    // to live tip, force live resync if poll_interval has elapsed.
    int64_t last_live_window_poll_ms_ = 0;
    int     last_cam_lookback_k_ = 0;
    bool    live_poll_deferred_ = false;
    // 0 = ok, 1 = soft cache pressure, 2 = hard (last-resort prune may run).
    int     cache_pressure_level_ = 0;
    // Rate-limit chunk pumps on the drain path (ms wall clock).
    int64_t last_chunk_pump_ms_ = 0;
    // Load-once: chunk from_ms successfully admitted (not mere HTTP attempt).
    // Re-GET only after fail/prune invalidation, or live tip force_newest.
    std::unordered_set<int64_t> history_slots_fetched_;
    // Priority variable-range patches (missing deps / eye) before bulk newest-first.
    std::vector<TimelineHole> timeline_holes_;
    // Sparse single-hash GETs — only after DepCritical ranges are idle.
    std::deque<std::string> single_block_q_;
    std::unordered_set<std::string> single_block_queued_;
    static constexpr int64_t kMinPatchMs = 8'000;
    static constexpr int64_t kMaxPatchMs = 180'000;
    static constexpr int kMaxSingleBlockQueue = 48;
    static constexpr int kMaxSingleBlockPerDrain = 2;
    // Verified segment disk cache (per domain); bootstrap + persist closed windows.
    SegmentDiskCache disk_cache_;
    bool disk_cache_bootstrapped_ = false;
    int  disk_cache_bootstrap_blocks_ = 0;
    // Chunked admit after load_recent (avoids multi-second hitch).
    std::vector<SegmentDiskCache::CachedBlock> bootstrap_pending_;
    size_t bootstrap_admit_i_ = 0;
    std::vector<int> bootstrap_segment_ids_;
    int bootstrap_g_live_ = -1;
    bool bootstrap_windows_marked_ = false;
    std::unordered_set<int> disk_segment_admitted_; // G complete on disk / admitted
    std::unordered_map<int, int> disk_cache_saved_count_; // G → last saved block count
    int64_t disk_cache_last_live_save_ms_ = 0;
    int64_t disk_cache_last_persist_ms_ = 0;
    char disk_cache_last_event_[160] = {};
    // Returned to live: fill missing sub-segments before tip seeds.
    bool live_catchup_active_ = false;
    // Open live tip 64s subsegment has been force-refreshed at least once this session.
    bool live_edge_refreshed_ = false;
    // Last completed/requested live open subseg from_ms (genesis-aligned grid).
    int64_t last_live_subseg_from_ = 0;
    // Deferred non-interval results when interval budget is preferred.
    std::deque<HttpIoPool::Result> deferred_fetch_results_;

    int poll_count_ = 0;
    int64_t last_poll_wall_ms_ = 0;
    int stats_verified_ok_ = 0;
    int stats_removed_ = 0;
    int stats_replaced_ = 0;
    int stats_detail_refilled_ = 0;
    int stats_confirmed_marks_ = 0;
    int stats_dag_floods_ = 0;
    int stats_api_is_main_ = 0;
    int stats_uncles_checked_ = 0;
    int stats_uncles_removed_ = 0;
    int stats_fetch_admitted_ = 0;
    int stats_trace_missing_ = 0;
    int seed_lane_rr_ = 0;
    // Endpoint throttle: pause interval enqueues until wall ms.
    int64_t http_backoff_until_ms_ = 0;
    int     http_backoff_streak_ = 0;
    long    last_interval_http_code_ = 0;
    int     stats_http_429_ = 0;
    int     stats_http_5xx_ = 0;

    static constexpr size_t kMaxSeedQueue = 512;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxFloodPerSeed = 256;
    static constexpr int kMaxFetchAdmitsPerDrain = 4; // hash/confirm path default
    // Interval timeline admits: higher so history ring applies before next pump.
    static constexpr int kIntervalAdmitsPerDrain = 24;
    static constexpr int kHistoryIntervalAdmitsPerPoll = 32;
    static constexpr int kMaxBfsNodesPerThreadSlice = 64;
    // History + live share disk subsegment grid (64s). Exactly 10 GETs per 640s G.
    // Live tip force-refreshes the open genesis-aligned 64s subsegment only.
    static constexpr int64_t kTimelineChunkMs = ALPH_SUBSEGMENT_MS;
    // drain_verify: at most one chunk every this many ms.
    static constexpr int64_t kChunkPumpIntervalMs = 400;
    // Soft cap when camera lookback index jumps (avoid enqueue storm).
    static constexpr int kMaxChunksOnCamStep = 2;
    static constexpr int kMaxChunksPerPoll = 4;
    static constexpr int kMaxChunksPerDrain = 2;
    // Load ring: disk-first body (15 G). Render ring is app-side (7 centered).
    static constexpr int kSegmentRingSize = ALPH_LOAD_RING_SEGMENTS;
    static constexpr int kInitialSegmentCount = 2; // bootstrap windows 0 and 1
    static constexpr int kBootstrapChunksPerPoll = 16;
    static constexpr int kBootstrapChunksPerDrain = 4;
    static constexpr int kPreTipChunksPerDrain = 2;
    static constexpr int kChunkMaxRetries = 3;
    // Past this fraction of current segment toward older → bump effective k.
    static constexpr float kPrefetchHysteresis = 0.40f;
    static constexpr int kAheadChunksPerDrain = 4;
    // Disk bootstrap admit budget per drain_verify tick.
    static constexpr int kBootstrapAdmitPerDrain = 2500;
};
