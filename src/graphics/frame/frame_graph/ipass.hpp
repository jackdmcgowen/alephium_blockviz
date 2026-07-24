#pragma once

// GPU pass interface: concrete passes own PSOs privately and plug into FrameTaskGraph.
// Multi-queue submit stays outside IPass (executor). See docs/layers/graphics.md.
//
// Profiling: when PassRecordParams::profiler is set, begin record() with:
//   frame_graph::PassProfileScope profile(*this, ctx);
// so F3 / bench see stable IPass::name() scopes (CPU + GPU).

#include "graphics/frame/frame_graph/frame_task_graph.hpp"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/core/queue_types.hpp"
#include "graphics/core/sampler.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class BufferManager;
class MeshArena;
class DebugDrawer;
struct ImDrawData;

namespace frame_graph
{

struct PassCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
    BufferManager* buffers = nullptr;
    const SamplerTable* samplers = nullptr;

    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool alpha_to_coverage = false;
    VkDescriptorSetLayout frame_ubo_layout = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t compute_family = 0;
    // Mesh cube PSO when device has mesh shaders; task = amplification frustum path.
    bool enable_mesh_cube = false;
    bool enable_mesh_task = false; // prefer task+mesh when hardware supports taskShader
    // UBO buffer handle for mesh path descriptor write (same frame UBO as classic).
    VkBuffer frame_ubo_buffer = VK_NULL_HANDLE;
    VkDeviceSize frame_ubo_range = 0;
};

// Extended per-frame recording inputs (filled by GraphicsSystem / frame_loop).
// Keeps PassContext small in frame_task_graph.hpp for topology-only use.
struct PassRecordParams
{
    PassContext base{};
    VkDescriptorSet frame_ubo_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE; // pre-cull or direct draw
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t instance_count = 0; // host-side upload count (pre-cull)
    uint32_t index_count = 36;
    // GPU frustum cull → compact instances + indirect draw (optional).
    bool use_gpu_instance_cull = false;
    VkBuffer visible_instance_buffer = VK_NULL_HANDLE;
    VkBuffer cull_draw_args_buffer = VK_NULL_HANDLE;
    // Mesh / task+mesh cube path (DrawMeshTasksEXT); false → classic VBO/IBO.
    bool use_mesh_cube_path = false;
    bool use_task_mesh_path = false; // amplification frustum; skip compute cull when true
    // Frustum planes for task shader (same convention as camera.cpp); valid if use_task_mesh.
    glm::vec4 cull_planes[6]{};
    float cull_half_extent = 1.05f;

    VkImage color_image = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageView resolve_color_view = VK_NULL_HANDLE;
    VkImage resolve_color_image = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkExtent2D scissor_extent{ 0, 0 };
    bool after_resize = false;
    bool transition_color_to_present = true;

    MeshArena* mesh_arena = nullptr;
    DebugDrawer* debug_drawer = nullptr;
    const glm::mat4* view_proj = nullptr;
    ImDrawData* imgui_draw_data = nullptr;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Picker
    uint32_t mouse_x = 0;
    uint32_t mouse_y = 0;
    bool picker_image_undefined = false;

    // Sobel outline
    VkBuffer outline_instance_buffer = VK_NULL_HANDLE;
    uint32_t outline_count = 0;

    FrameProfiler* profiler = nullptr;
};

class IPass
{
public:
    virtual ~IPass() = default;

    virtual const char* name() const = 0;
    virtual QueueType queue() const = 0;

    virtual void create(const PassCreateInfo& info) = 0;
    virtual void destroy(VkDevice device) = 0;
    virtual void recreate(const PassCreateInfo& info)
    {
        destroy(info.device);
        create(info);
    }

    // Record into ctx.base.cmd (caller owns begin/end of CB unless pass documents otherwise).
    virtual void record(const PassRecordParams& ctx) = 0;

    virtual void declare_resources(std::vector<ResourceId>& /*reads*/,
                                   std::vector<ResourceId>& /*writes*/) const
    {
    }
};

// RAII: CPU + GPU scopes named after IPass::name(). No-op when profiler is null/disabled.
struct PassProfileScope
{
    FrameProfiler::CpuScope cpu;
    FrameProfiler::GpuScope gpu;

    PassProfileScope(const IPass& pass, const PassRecordParams& p)
        : cpu(p.profiler, pass.name())
        , gpu(p.profiler,
              p.base.cmd,
              pass.name(),
              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
              VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)
    {
    }
};

} // namespace frame_graph
