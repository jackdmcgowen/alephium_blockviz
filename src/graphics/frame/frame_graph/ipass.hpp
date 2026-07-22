#pragma once

// GPU pass interface: concrete passes own PSOs privately and plug into FrameTaskGraph.
// Multi-queue submit stays outside IPass (executor). See docs/layers/graphics.md.

#include "graphics/frame/frame_graph/frame_task_graph.hpp"
#include "graphics/queue_types.hpp"
#include "graphics/sampler.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class BufferManager;
class MeshArena;
class DebugDrawer;
class FrameProfiler;
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
};

// Extended per-frame recording inputs (filled by GraphicsSystem / frame_loop).
// Keeps PassContext small in frame_task_graph.hpp for topology-only use.
struct PassRecordParams
{
    PassContext base{};
    VkDescriptorSet frame_ubo_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t instance_count = 0;
    uint32_t index_count = 36;

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

} // namespace frame_graph
