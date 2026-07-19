#pragma once

// Host-side scene → frame builder. Runs on the engine render thread via IFrameSource.
// Living docs: docs/layers/app.md (map: docs/layers/README.md).
//
// Production BlockFlow visual model (spacing/layout unchanged — keep):
//   solid α  — confirmed bag with all deps live
//   green    — per-lane frontier tip H_c (or walk-anim display) + full blockDeps arrows
//   cyan     — unconfirmed height>H_c that deps a domain frontier tip + link arrows into tip
//   orange   — missing-dep incompletes (not green/cyan)
//   gold     — selection
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
    // Selection multi-hop: defaults; runtime overrides via StyleBlockflow JSON.
    static constexpr float  kWalkHopGrowSec    = 0.12f;
    static constexpr float  kWalkSlotStagger   = 0.03f;
    static constexpr float  kWalkDieFadeSec    = 0.22f;
    static constexpr float  kWalkArrivedHoldSec = 0.02f;
    static constexpr int    kWalkMaxHops       = 32;
    static constexpr int    kWalkSlotCount     = ALPH_NUM_GROUPS * 2 - 1; // 2G−1

    // Growing → Held → Dying (remove only). No unused Fading/Gone.
    enum class ArrowPhase : uint8_t { Growing, Held, Dying };

    enum class WalkSlotState : uint8_t
    {
        Pending = 0, // waiting start stagger
        Flying  = 1, // hop arrow growing
        Arrived = 2, // brief hold then next hop
        Dying   = 3, // last hop red + alpha fade
        Dead    = 4,
    };

    // Shard-rejoin TRACE: Leave = H/B→cross-shard dep; Rejoin = dep→same shard S.
    enum class WalkHopKind : uint8_t { Leave = 0, Rejoin = 1 };

    // Future: GroupHistory, DirectFan — v1 is ShardRejoin only.
    enum class TraceType : uint8_t { ShardRejoin = 0 };

    struct DepWalkSlot
    {
        int           slot = 0;          // sticky clique index 0..2G-2
        int           orig_from = 0;     // original selected shard S
        int           orig_to = 0;
        WalkHopKind   last_hop = WalkHopKind::Leave; // hop just completed / in flight
        std::string   from_hash;
        std::string   to_hash;
        glm::vec3     from_pos{ 0.f };
        glm::vec3     to_pos{ 0.f };
        bool          has_pos = false;
        bool          ghost_target = false; // to is missing — die after first draw
        float         grow = 0.f;
        float         die_alpha = 1.f;
        float         state_start_sec = -1.f;
        float         delay = 0.f;
        int           hops_done = 0;
        WalkSlotState state = WalkSlotState::Pending;
        std::unordered_set<std::string> visited; // cycle guard
    };

    struct DepArrowAnim
    {
        float      birth_sec      = -1.f;
        float      fade_start_sec = 0.f;
        ArrowPhase phase          = ArrowPhase::Growing;
        glm::vec3  from_pos{ 0.f };
        glm::vec3  to_pos{ 0.f };
        bool       has_pos        = false;
        float      base_alpha     = 0.95f;
        float      tip_scale      = 1.f;
        // 0 = cyan (frontier child link), 1 = green (frontier tip deps). Lerps on role change.
        float      confirm_blend_t         = 0.f;
        float      confirm_blend_from      = 0.f;
        float      confirm_blend_start_sec = -1.f;
        bool       want_green              = false;
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
                                const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_set<std::string>& drawn_set,
                                const std::unordered_set<std::string>& green_display,
                                const std::unordered_set<std::string>& cyan_owners,
                                const std::unordered_set<std::string>& frontier_domain,
                                float tip_len, float tip_rad, float shaft_r, float clearance);

    // Short colored BFS paths (no edge soup; both ends must be drawn).
    void draw_bfs_traces_(DebugDrawer& debug,
                          const std::unordered_map<std::string, glm::vec3>& positions,
                          const std::unordered_set<std::string>& drawn_set);

    void update_death_and_walk_(const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_map<std::string, glm::vec3>& positions,
                                float now);

    float ephemeral_grow_u_(const std::string& key, float stagger_delay,
                            std::unordered_set<std::string>& seen);

    void restart_dep_walk_(const std::string& root_hash,
                           const std::unordered_map<std::string, glm::vec3>& positions);
    void clear_dep_walk_();
    void tick_dep_walk_(float now,
                        const std::unordered_map<std::string, glm::vec3>& positions);
    void draw_dep_walk_(DebugDrawer& debug, float tip_len, float tip_rad, float shaft_r,
                        float clearance);
    // Shard (chain_idx) for hash; 255 if unknown.
    int chain_idx_for_hash_(const std::string& hash) const;
    bool hash_on_shard_(const std::string& hash, int from, int to) const;
    // Resolve chainFrom/chainTo for hash; false if unknown.
    bool shard_of_hash_(const std::string& hash, int& from_out, int& to_out) const;
    // Deps of node sorted by chain_idx then hash (stable leave-slot order).
    std::vector<std::string> sorted_deps_(const std::string& node_hash) const;
    // Leave hop: sticky slot index into sorted_deps_.
    std::string next_walk_dep_for_slot_(const std::string& node_hash, int slot,
                                        const std::unordered_set<std::string>& visited) const;
    // Rejoin hop: dep of node on shard [from→to] (caller passes reverse of leave target).
    std::string find_dep_on_shard_(const std::string& node_hash, int from, int to,
                                   const std::unordered_set<std::string>& visited) const;
    void collect_walk_force_hashes_(std::unordered_set<std::string>& out) const;
    bool dep_walk_active_() const;

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

    // Selection multi-hop walk (2G−1 slots).
    std::string              walk_root_hash_;
    std::vector<DepWalkSlot> walk_slots_;
    bool                     walk_seeded_from_detail_ = false;
    uint64_t                 last_walk_replay_gen_ = 0;
    // TRACE Sobel strength on visited nodes (fade after all slots dead).
    std::unordered_map<std::string, float> walk_visited_sobel_;
    bool  walk_sobel_fading_ = false;
    float walk_sobel_fade_start_sec_ = -1.f;
};
