#pragma once

// Selection outline depth+color redraw (all requested cubes). Owns outline PSO (IPass).
// Images live on SobelResources; multi-queue submit stays in SobelAsyncPass.
#include "graphics/frame/frame_graph/ipass.hpp"
#include "graphics/frame/passes/sobel_resources.hpp"

namespace frame_graph
{

class OutlinePass final : public IPass
{
public:
    const char* name() const override { return "SelectionDepth"; }
    QueueType queue() const override { return QueueType::_3D; }

    void attach(SobelResources* res) { res_ = res; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;
    void recreate(const PassCreateInfo& info) override;

    void record(const PassRecordParams& ctx) override;

    void declare_resources(std::vector<ResourceId>& reads,
                           std::vector<ResourceId>& writes) const override;

    // After outline: release sel_depth for CMP; outline_color → SHADER_READ for overlay.
    void record_sel_depth_release_for_compute(VkCommandBuffer cmd) const;
    void record_outline_color_to_shader_read(VkCommandBuffer cmd) const;

    bool ready() const { return outline_pipeline_ != VK_NULL_HANDLE && res_ && res_->ready(); }

private:
    void create_pipeline_(const PassCreateInfo& info);
    void destroy_pipeline_(VkDevice device);

    SobelResources* res_ = nullptr;
    VkPipelineLayout outline_layout_ = VK_NULL_HANDLE;
    VkPipeline outline_pipeline_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
