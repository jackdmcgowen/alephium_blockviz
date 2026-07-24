#pragma once

// Main color+depth pass: cubes + debug mesh. Owns cube PSOs privately (IPass).
// Classic: instanced VBO/IBO (+ optional GPU cull → DrawIndexedIndirect).
// Mesh-only: VK_EXT_mesh_shader (compute frustum).
// Task+mesh: amplification (task) frustum + mesh face/cone cull (preferred).
#include "graphics/frame/frame_graph/ipass.hpp"

namespace frame_graph
{

enum class CubeDrawPath : uint8_t
{
    ClassicInstanced = 0,
    MeshOnly         = 1,
    TaskMesh         = 2, // amplification + mesh
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

    void transition_color_to_present(VkCommandBuffer cmd, VkImage color_image) const;

    void begin_command_buffer(VkCommandBuffer cmd, class FrameProfiler* profiler) const;
    void end_command_buffer(VkCommandBuffer cmd) const;

    VkPipeline cube_pipeline() const { return cube_pipeline_; }
    VkPipelineLayout cube_layout() const { return cube_layout_; }
    bool ready() const { return cube_pipeline_ != VK_NULL_HANDLE; }

    bool mesh_cube_ready() const
    {
        return (mesh_only_pipeline_ != VK_NULL_HANDLE || task_mesh_pipeline_ != VK_NULL_HANDLE) &&
               pfn_draw_mesh_tasks_ != nullptr;
    }
    bool task_mesh_ready() const
    {
        return task_mesh_pipeline_ != VK_NULL_HANDLE && pfn_draw_mesh_tasks_ != nullptr;
    }
    CubeDrawPath preferred_cube_path() const
    {
        if (task_mesh_ready())
            return CubeDrawPath::TaskMesh;
        if (mesh_only_pipeline_ != VK_NULL_HANDLE && pfn_draw_mesh_tasks_)
            return CubeDrawPath::MeshOnly;
        return CubeDrawPath::ClassicInstanced;
    }

private:
    void create_cube_pipeline_(const PassCreateInfo& info);
    void destroy_cube_pipeline_(VkDevice device);
    void create_mesh_pipelines_(const PassCreateInfo& info);
    void destroy_mesh_pipelines_(VkDevice device);
    void write_mesh_only_descriptors_(VkBuffer instances, VkBuffer draw_args);
    void write_task_mesh_descriptors_(VkBuffer instances);

    VkPipeline cube_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout cube_layout_ = VK_NULL_HANDLE;

    // Mesh-only (no task): compute cull + mesh_only.mesh
    VkPipeline mesh_only_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout mesh_only_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout mesh_only_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool mesh_only_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet mesh_only_set_ = VK_NULL_HANDLE;
    VkBuffer mesh_only_bound_inst_ = VK_NULL_HANDLE;
    VkBuffer mesh_only_bound_draw_ = VK_NULL_HANDLE;

    // Task + mesh (amplification frustum + face cone)
    VkPipeline task_mesh_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout task_mesh_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout task_mesh_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool task_mesh_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet task_mesh_set_ = VK_NULL_HANDLE;
    VkBuffer task_mesh_bound_inst_ = VK_NULL_HANDLE;

    VkBuffer mesh_ubo_buffer_ = VK_NULL_HANDLE;
    PFN_vkCmdDrawMeshTasksEXT pfn_draw_mesh_tasks_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    bool want_task_ = false;
};

} // namespace frame_graph
