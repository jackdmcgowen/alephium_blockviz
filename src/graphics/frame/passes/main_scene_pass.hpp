#pragma once

// Main color+depth pass: cubes + debug mesh. Owns cube PSO privately (IPass).
#include "graphics/frame/frame_graph/ipass.hpp"

namespace frame_graph
{

class MainScenePass final : public IPass
{
public:
    const char* name() const override { return "MainColorDepth"; }
    QueueType queue() const override { return QueueType::_3D; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;
    void recreate(const PassCreateInfo& info) override;

    void record(const PassRecordParams& ctx) override;

    void declare_resources(std::vector<ResourceId>& reads,
                           std::vector<ResourceId>& writes) const override;

    // Access for transition helpers still used by sobel path (present defer).
    void transition_color_to_present(VkCommandBuffer cmd, VkImage color_image) const;

    // Begin main CB (profiler reset). Caller may chain pick then end().
    void begin_command_buffer(VkCommandBuffer cmd, class FrameProfiler* profiler) const;
    void end_command_buffer(VkCommandBuffer cmd) const;

    VkPipeline cube_pipeline() const { return cube_pipeline_; }
    VkPipelineLayout cube_layout() const { return cube_layout_; }
    bool ready() const { return cube_pipeline_ != VK_NULL_HANDLE; }

private:
    void create_cube_pipeline_(const PassCreateInfo& info);
    void destroy_cube_pipeline_(VkDevice device);

    VkPipeline cube_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cube_layout_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
