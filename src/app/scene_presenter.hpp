#pragma once

// Host-side scene → frame builder (E14). Runs on the engine render thread via IFrameSource.
#include "domain/block_scene.hpp"
#include "domain/layout.hpp"
#include "engine/engine.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

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
    static constexpr float  kArrowFadeSec   = 0.32f;
    static constexpr float  kArrowStagger   = 0.045f; // seconds between sibling deps

    enum class ArrowPhase : uint8_t { Growing, Held, Fading, Gone };

    struct DepArrowAnim
    {
        float      birth_sec      = -1.f; // <0 = not started; else time grow begins
        float      fade_start_sec = 0.f;
        ArrowPhase phase          = ArrowPhase::Growing;
        glm::vec3  from_pos{ 0.f };
        glm::vec3  to_pos{ 0.f };
        bool       has_pos        = false;
        float      base_alpha     = 0.95f;
        float      tip_scale      = 1.f;
        // Confirm color blend (0 = cyan, 1 = green). Updated only while tip-active;
        // Fading/Gone freeze last confirm_blend_t (no re-grow on confirm).
        bool  tip_confirmed           = false;
        float confirm_blend_t         = 0.f;
        float confirm_blend_from      = 0.f;
        float confirm_blend_start_sec = -1.f; // <0 = no active blend
    };

    float now_sec_() const;
    // Tip/frontier deps: grow once → hold while listing is tip → fade on supersede.
    void tip_dep_tick_and_draw_(DebugDrawer& debug,
                                const std::unordered_map<std::string, glm::vec3>& positions,
                                const std::unordered_set<std::string>& live_nodes,
                                const std::unordered_set<std::string>& frontier_set,
                                float tip_len, float tip_rad, float shaft_r, float clearance);

    // Ephemeral selection/hover: simple grow; OK to forget when not drawn.
    float ephemeral_grow_u_(const std::string& key, float stagger_delay,
                            std::unordered_set<std::string>& seen);

    BlockScene& scene_;
    PolarShardLayout layout_;

    std::chrono::steady_clock::time_point clock0_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::string, DepArrowAnim> tip_dep_anims_;
    std::unordered_map<std::string, float>        ephemeral_birth_sec_;
};
