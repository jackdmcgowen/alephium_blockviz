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
// Timeline GETs are ~60s chunks (newest-first, budgeted); Steady live refreshes tip chunk only.
// Presentation (solid/green/cyan/orange/gold) lives in ScenePresenter — not here.
#include "network/alephium/block_fetch_pool.hpp"
#include "network/alephium/main_chain_cache.hpp"
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

    void on_start();
    // Publish loading/activity HUD into BlockScene (call after poll/drain).
    void publish_hud(int domain, const char* base_url, bool switching = false);

    void poll_once(int64_t& last_poll_ts);

    // False during IdentifyTips / BfsTrace (and before first poll done).
    bool ready_for_poll() const;

    void drain_verify(int max_jobs, const std::atomic<bool>& running);

    void maybe_refill_selection_detail();

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
    // Steady + live window fully chunk-filled (gate deep history ≥2).
    bool live_tip_pipeline_ready_() const;
    // Dual-segment initial: windows 0 and 1 both polled.
    bool dual_initial_complete_() const;
    // Triple-buffer ring of absolute lookback indices (≤ kSegmentRingSize).
    void update_segment_ring_();
    bool is_active_segment_(int index) const;
    // Lookback k with prefetch hysteresis (start k+1/k+2 before full cross).
    int  effective_lookback_index_() const;
    // On successful interval admit: advance fill cursor for owning window(s).
    void on_interval_chunk_admitted_(int64_t from_ms, int64_t to_ms);
    void on_interval_chunk_failed_(int64_t from_ms);

    void drain_fetch_results_(int max_admits);
    // Live-chain only: per-hash GET /blocks/{hash}.
    bool enqueue_missing_dep_(const std::string& dep_hash);
    // On confirm: live → hash-fill deps; historical → time-slot poll (no hash).
    int  enqueue_confirm_deps_(const std::string& parent_hash);
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
    // Mark in-pool deps of a confirmed block as main (no is_main API).
    void propagate_main_from_confirmed_deps_(const std::string& confirmed_hash);
    // If tip is > H_c+1, walk deps to current frontier; confirm path; set walk anim.
    bool try_chain_walk_confirm_(const std::string& tip_hash, uint32_t lane, int height);
    void prune_detail_store();
    // Soft-evict graph nodes older than active ring − 1 window (Steady only).
    // Clears matching history_slots_fetched_ keys so re-visit re-fetches chunks.
    void maybe_ring_retain_prune_();

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
    // Active fetch ring (absolute indices); size ≤ kSegmentRingSize.
    int active_ring_[3]{};
    int active_ring_n_ = 0;
    int64_t genesis_ms_ = ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    bool    genesis_resolved_ = false;
    // Live-chain poll gate: skip window-0 API while camera lookback k > 0; on return
    // to live tip, force live resync if poll_interval has elapsed.
    int64_t last_live_window_poll_ms_ = 0;
    int     last_cam_lookback_k_ = 0;
    bool    live_poll_deferred_ = false;
    // Rate-limit chunk pumps on the drain path (ms wall clock).
    int64_t last_chunk_pump_ms_ = 0;
    // Quantized chunk from_ts of interval polls already issued (dedupe).
    std::unordered_set<int64_t> history_slots_fetched_;

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

    static constexpr size_t kMaxSeedQueue = 512;
    static constexpr int kTipRefreshEveryNPolls = 3;
    static constexpr int kMaxFloodPerSeed = 256;
    static constexpr int kMaxFetchAdmitsPerDrain = 4; // selection path only
    static constexpr int kMaxBfsNodesPerThreadSlice = 64;
    // Sub-interval for blocks-with-events GETs (~10 chunks per 10 min segment).
    static constexpr int64_t kTimelineChunkMs = 60'000;
    // drain_verify: at most one chunk every this many ms.
    static constexpr int64_t kChunkPumpIntervalMs = 400;
    static constexpr int kMaxChunksPerPoll = 4;
    static constexpr int kMaxChunksPerDrain = 2;
    // Triple-buffer ring: at most 3 active lookback windows for fetch/HUD/draw.
    static constexpr int kSegmentRingSize = 3;
    static constexpr int kInitialSegmentCount = 2; // bootstrap windows 0 and 1
    static constexpr int kBootstrapChunksPerPoll = 16;
    static constexpr int kBootstrapChunksPerDrain = 4;
    static constexpr int kPreTipChunksPerDrain = 2;
    static constexpr int kChunkMaxRetries = 3;
    // Past this fraction of current segment toward older → bump effective k.
    static constexpr float kPrefetchHysteresis = 0.40f;
    static constexpr int kAheadChunksPerDrain = 4; // prioritize filling k+1,k+2
};
