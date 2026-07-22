#pragma once

// Async Sobel compute: sample selection depth → white edge mask. Owns CMP PSO (IPass).
#include "graphics/frame/frame_graph/ipass.hpp"
#include "graphics/frame/passes/sobel_resources.hpp"

namespace frame_graph
{

class SobelComputePass final : public IPass
{
public:
    const char* name() const override { return "SobelAsyncCMP"; }
    QueueType queue() const override { return QueueType::CMP; }

    void attach(SobelResources* res) { res_ = res; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;
    void recreate(const PassCreateInfo& info) override;

    void record(const PassRecordParams& ctx) override;

    void declare_resources(std::vector<ResourceId>& reads,
                           std::vector<ResourceId>& writes) const override;

    bool ready() const { return pipeline_ != VK_NULL_HANDLE && res_ && res_->ready(); }

private:
    void create_pipeline_(VkDevice device);
    void destroy_pipeline_(VkDevice device);

    SobelResources* res_ = nullptr;
    VkPipelineLayout compute_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
