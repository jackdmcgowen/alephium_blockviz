#pragma once

// Host-side scene → frame builder. Runs on the engine render thread via IFrameSource.
// Living docs: docs/layers/app.md (map: docs/layers/README.md).
//
// Production BlockFlow visual model (spacing/layout unchanged — keep):
//   solid α  — confirmed bag with all deps live
//   green    — per-lane frontier tip H_c (or walk-anim display) + full blockDeps arrows
//   red      — unconfirmed height>H_c that deps a domain frontier tip + link arrows into tip
//   orange   — missing-dep incompletes (not green/red unconfirmed)
//   gold     — selection + full BFS block-dep fan from selected root
//   red      — removal death fade
//   BFS rays — thin stylized lines per parallel confirm thread (N=2G-1)
#include "domain/block_scene.hpp"
#include "domain/layout.hpp"
#include "engine/engine.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

class ScenePresenter : public IFrameSource
{
public:
    explicit ScenePresenter(BlockScene& scene);

    void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                 DebugDrawer* debug) override;

private:
    static constexpr size_t kMaxInstances   = 1024 * 1024;
    static constexpr float  kArrowGrowSec   = 0.40f;
    static constexpr float  kArrowStagger   = 0.045f;
    static constexpr float  kDeathSec       = 0.45f;
    static constexpr float  kWalkStepSec    = 0.22f;
    static constexpr float  kConfirmBlendSec = 0.35f;
    static constexpr float  kSegFadeInSec   = 0.28f;
    static constexpr float  kSegFadeOutSec  = 0.32f;
    static constexpr float  kCyanToMainSec  = 0.40f; // unconfirmed red → main dual lerp
    static constexpr float  kSecondaryAlphaSec = 0.50f;
    static constexpr float  kTipReplaceFadeSec = 0.32f;
    static constexpr float  kTipSolidAlpha = 0.90f;
    static constexpr float  kTipSecondaryAlpha = 0.40f; // floor > 0
    static constexpr uint32_t kMaxTipDepArrows = 2048;
    // Selection BFS dep fan: multi-segment DAG + snappy level-wave anim.
    static constexpr int    kMaxSelDepNodes       = 4096;
    static constexpr int    kMaxSelDepEdges       = 8192;
    static constexpr int    kMaxSelDepArrows      = 8192; // match edge cap (own budget)
    static constexpr float  kSelDepGrowSec        = 0.08f;
    static constexpr float  kSelDepLevelStagger   = 0.012f; // delay per BFS depth
    static constexpr float  kSelDepEdgeStagger    = 0.0015f; // within-level micro delay
    static constexpr float  kSelDepMaxStaggerSec  = 0.25f;  // hard cap — fan never waits seconds

    // Growing → Held → Fading (replaced) | Dying (block removed). Never re-grow.
    enum class ArrowPhase : uint8_t { Growing, Held, Fading, Dying };
    enum class TipTier : uint8_t { Primary, Secondary, Unconfirmed };

    // Full BFS of block deps from selected root (static gold fan).
    struct SelectionDepEdge
    {
        std::string from;
        std::string to;
        int         depth = 0; // depth of `from` (edge spans depth → depth+1)
    };

    struct SelectionDepTrace
    {
        std::string                   root;
        std::vector<std::string>      nodes;
        std::unordered_set<std::string> node_set;
        std::vector<SelectionDepEdge> edges;
        bool                          seeded_from_detail = false;
    };

    struct DepArrowAnim
    {
        float      birth_sec      = -1.f;
        float      fade_start_sec = 0.f;
        ArrowPhase phase          = ArrowPhase::Growing;
        glm::vec3  from_pos{ 0.f };
        glm::vec3  to_pos{ 0.f };
        bool       has_pos        = false;
        float      tip_scale      = 1.f;
        // Dual main RGBA (listing shaft / dep tip); white tip if dep missing.
        glm::vec4  shaft_rgba{ 1.f, 1.f, 1.f, 0.9f };
        glm::vec4  tip_rgba{ 1.f, 1.f, 1.f, 0.9f };
        TipTier    tier = TipTier::Primary;
        // Unconfirmed: 0 = pure cyan dual, 1 = full main dual.
        float      cyan_to_main_u = 1.f;
        float      cyan_to_main_start = -1.f;
        // Secondary: 0 = solid, 1 = translucent floor applied.
        float      secondary_alpha_u = 0.f;
        float      secondary_fade_start = -1.f;
    };

    struct DyingBlock
    {
        std::string hash;
        glm::vec3   pos{ 0.f };
        glm::vec3   color{ 1.f, 0.12f, 0.1f };
        float       birth_sec = 0.f;
        float       scale = 1.f;
    };

    struct FrontierWalkAnim
    {
        std::vector<std::string> path; // old → new
        size_t index = 0;
        float  step_start_sec = -1.f;
    };

    float now_sec_() const;
    void tip_dep_tick_and_draw_(DebugDrawer& debug,
                                const std::unordered_map<std::string, glm::vec3>& positions,
                                const std::unordered_map<std::string, glm::vec3>& block_colors,
                                const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_set<std::string>& soft_evicted,
                                const std::unordered_set<std::string>& drawn_set,
                                const std::unordered_set<std::string>& green_display,
                                const std::unordered_set<std::string>& cyan_owners,
                                const std::unordered_set<std::string>& unconfirmed_tips,
                                const std::unordered_set<std::string>& frontier_domain,
                                float tip_len, float tip_rad, float shaft_r, float clearance);

    // Per-lane previous primary tip hash (for secondary translucent fade).
    std::string prev_primary_tip_[BlockScene::kLaneCount]{};

    // Short colored BFS paths (no edge soup; both ends must be drawn).
    void draw_bfs_traces_(DebugDrawer& debug,
                          const std::unordered_map<std::string, glm::vec3>& positions,
                          const std::unordered_set<std::string>& drawn_set);

    void update_death_and_walk_(const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_map<std::string, glm::vec3>& positions,
                                const std::unordered_set<std::string>& soft_evicted,
                                float now);

    float ephemeral_grow_u_(const std::string& key, float stagger_delay,
                            std::unordered_set<std::string>& seen,
                            float grow_sec = kArrowGrowSec);

    void clear_selection_deps_();
    void rebuild_selection_dep_bfs_(const std::string& root_hash);
    void collect_selection_dep_force_(std::unordered_set<std::string>& out) const;
    // Shard (chain_idx) for hash; 255 if unknown.
    int chain_idx_for_hash_(const std::string& hash) const;
    // Deps of node sorted by chain_idx then hash (stable BFS expand + gold order).
    std::vector<std::string> sorted_deps_(const std::string& node_hash) const;

    BlockScene& scene_;
    PolarShardLayout layout_;

    std::chrono::steady_clock::time_point clock0_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::string, DepArrowAnim> tip_dep_anims_;
    std::unordered_map<std::string, float>        ephemeral_birth_sec_;

    std::unordered_set<std::string> prev_live_nodes_;
    std::unordered_map<std::string, glm::vec3> prev_positions_;
    std::vector<DyingBlock> dying_blocks_;
    FrontierWalkAnim walk_by_lane_[BlockScene::kLaneCount]{};
    // Sliding-window segment fade by lookback k (0 = live): 0..1 for enter/leave.
    std::unordered_map<int, float> seg_fade_alpha_;
    float last_seg_fade_sec_ = -1.f;

    // Rebuild layout only when graph generation / timeline origin change.
    struct LayoutCache
    {
        uint64_t graph_gen = 0;
        int64_t  origin_ms = 0;
        int64_t  genesis_ms = 0;
        int64_t  window_ms = 0;
        LayoutResult layout;
        std::unordered_set<std::string> live_nodes;
        // G_seg → indices into layout.placements
        std::unordered_map<int, std::vector<size_t>> by_g;
        // hash → placement index (force-draw without full scan)
        std::unordered_map<std::string, size_t> by_hash;
    };
    LayoutCache layout_cache_;

    // Selection full BFS block-dep fan.
    SelectionDepTrace sel_dep_;
    uint64_t          last_walk_replay_gen_ = 0;
};
