#pragma once

// Fullscreen edge×color overlay onto swapchain color. Owns overlay PSO (IPass).
#include "graphics/frame/frame_graph/ipass.hpp"
#include "graphics/frame/passes/sobel_resources.hpp"

namespace frame_graph
{

class EdgeOverlayPass final : public IPass
{
public:
    const char* name() const override { return "EdgeOverlay"; }
    QueueType queue() const override { return QueueType::_3D; }

    void attach(SobelResources* res) { res_ = res; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;
    void recreate(const PassCreateInfo& info) override;

    void record(const PassRecordParams& ctx) override;

    void declare_resources(std::vector<ResourceId>& reads,
                           std::vector<ResourceId>& writes) const override;

    // Acquire edge image for graphics sampling (after compute_finished wait).
    void record_edge_acquire_for_graphics(VkCommandBuffer cmd) const;

    bool ready() const { return overlay_pipeline_ != VK_NULL_HANDLE && res_ && res_->ready(); }

private:
    void create_pipeline_(const PassCreateInfo& info);
    void destroy_pipeline_(VkDevice device);

    SobelResources* res_ = nullptr;
    VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
    VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
