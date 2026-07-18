#pragma once

// Host-side scene → frame builder. Runs on the engine render thread via IFrameSource.
//
// Production BlockFlow visual model (spacing/layout unchanged — keep):
//   solid α  — confirmed bag with all deps live
//   green    — per-lane frontier tip H_c (or walk-anim display) + full blockDeps arrows
//   cyan     — unconfirmed height>H_c that deps a domain frontier tip + link arrows into tip
//   orange   — missing-dep incompletes (not green/cyan)
//   gold     — selection
//   red      — removal death fade
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

    // Growing → Held → Dying (remove only). No unused Fading/Gone.
    enum class ArrowPhase : uint8_t { Growing, Held, Dying };

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
                                const std::unordered_set<std::string>& green_display,
                                const std::unordered_set<std::string>& cyan_owners,
                                const std::unordered_set<std::string>& frontier_domain,
                                float tip_len, float tip_rad, float shaft_r, float clearance);

    void update_death_and_walk_(const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_map<std::string, glm::vec3>& positions,
                                float now);

    float ephemeral_grow_u_(const std::string& key, float stagger_delay,
                            std::unordered_set<std::string>& seen);

    BlockScene& scene_;
    PolarShardLayout layout_;

    std::chrono::steady_clock::time_point clock0_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::string, DepArrowAnim> tip_dep_anims_;
    std::unordered_map<std::string, float>        ephemeral_birth_sec_;

    std::unordered_set<std::string> prev_live_nodes_;
    std::unordered_map<std::string, glm::vec3> prev_positions_;
    std::vector<DyingBlock> dying_blocks_;
    FrontierWalkAnim walk_by_lane_[BlockScene::kLaneCount]{};
};
