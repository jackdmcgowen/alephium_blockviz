#pragma once

// Main color+depth pass: cubes + debug mesh. Owns cube PSOs privately (IPass).
// Classic: instanced VBO/IBO (+ optional GPU cull → DrawIndexedIndirect).
// Mesh (PR3): VK_EXT_mesh_shader cube meshlets from instance SSBO when enabled.
#include "graphics/frame/frame_graph/ipass.hpp"

namespace frame_graph
{

enum class CubeDrawPath : uint8_t
{
    ClassicInstanced = 0,
    MeshShader       = 1,
};

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
    bool mesh_cube_ready() const
    {
        return mesh_pipeline_ != VK_NULL_HANDLE && pfn_draw_mesh_tasks_ != nullptr;
    }
    CubeDrawPath preferred_cube_path() const
    {
        return mesh_cube_ready() ? CubeDrawPath::MeshShader : CubeDrawPath::ClassicInstanced;
    }

private:
    void create_cube_pipeline_(const PassCreateInfo& info);
    void destroy_cube_pipeline_(VkDevice device);
    void create_mesh_cube_pipeline_(const PassCreateInfo& info);
    void destroy_mesh_cube_pipeline_(VkDevice device);
    void write_mesh_descriptors_(VkBuffer instances, VkBuffer draw_args);

    VkPipeline cube_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cube_layout_ = VK_NULL_HANDLE;

    // Mesh path (optional).
    VkPipeline mesh_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout mesh_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout mesh_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool mesh_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet mesh_set_ = VK_NULL_HANDLE;
    VkBuffer mesh_bound_instances_ = VK_NULL_HANDLE;
    VkBuffer mesh_bound_draw_args_ = VK_NULL_HANDLE;
    VkBuffer mesh_ubo_buffer_ = VK_NULL_HANDLE;
    PFN_vkCmdDrawMeshTasksEXT pfn_draw_mesh_tasks_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
