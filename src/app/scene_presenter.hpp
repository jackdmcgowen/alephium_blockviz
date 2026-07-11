#pragma once

// Host-side scene → frame builder (E14). Runs on the engine render thread via IFrameSource.
#include "domain/block_scene.hpp"
#include "domain/layout.hpp"
#include "engine/blockviz_engine_api.hpp"

class ScenePresenter : public IFrameSource
{
public:
    explicit ScenePresenter(BlockScene& scene);

    void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                 DebugDrawer* debug) override;

private:
    static constexpr size_t kMaxInstances = 1024 * 1024;

    BlockScene& scene_;
    PolarShardLayout layout_;
};
