#pragma once

// Host-side scene → frame builder (E14). Runs on the engine render thread via IFrameSource.
#include "domain/block_scene.hpp"
#include "domain/layout.hpp"
#include "engine/blockviz_engine_api.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ScenePresenter : public IFrameSource
{
public:
    explicit ScenePresenter(BlockScene& scene);

    void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                 DebugDrawer* debug) override;

private:
    static constexpr size_t kMaxInstances = 1024 * 1024;
    static constexpr float  kArrowGrowSec = 0.38f;
    static constexpr float  kArrowStagger = 0.045f; // seconds between sibling deps

    // Birth time (steady seconds) for grow animation; key = kind|from|to
    float arrow_grow_u_(const std::string& key, float stagger_delay,
                        std::unordered_set<std::string>& seen_this_frame);

    BlockScene& scene_;
    PolarShardLayout layout_;

    std::chrono::steady_clock::time_point clock0_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::string, float> arrow_birth_sec_;
};
