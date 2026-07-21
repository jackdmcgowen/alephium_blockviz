#pragma once

// Main-pass command recording: barriers, cubes, debug mesh, ImGui, present transition (E12).
// Does not own pick policy — caller may record a picker pass after record_main, then end().
#include <vulkan/vulkan.h>

#include <cstdint>
#include <glm/glm.hpp>

class DebugDrawer;
class MeshArena;
class FrameProfiler;
struct ImDrawData;

struct FrameRecordParams
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t image_index = 0;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool after_resize = false;

    uint32_t width = 0;
    uint32_t height = 0;
    VkExtent2D scissor_extent{ 0, 0 };

    VkImage color_image = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;
    VkImage depth_image = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;

    // G4 MSAA: when samples > 1, color_view/depth_view are MSAA targets;
    // resolve_color_view is the single-sample swapchain view.
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageView resolve_color_view = VK_NULL_HANDLE;
    VkImage resolve_color_image = VK_NULL_HANDLE; // for present transition

    VkPipeline cube_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout cube_layout = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t instance_count = 0;
    uint32_t index_count = 36;

    // Optional debug geometry (upload+draw inside the main pass)
    MeshArena* mesh_arena = nullptr;
    DebugDrawer* debug_drawer = nullptr;
    const glm::mat4* view_proj = nullptr;

    // Optional ImGui draw data (already built for this frame)
    ImDrawData* imgui_draw_data = nullptr;

    // When false, color stays COLOR_ATTACHMENT_OPTIMAL for post (async Sobel overlay).
    bool transition_color_to_present = true;

    // Optional frame profiler (GPU timestamps + MeshArenaUpload CPU).
    FrameProfiler* profiler = nullptr;
};

class FrameRecorder
{
public:
    // vkBeginCommandBuffer + main dynamic-rendering pass.
    // Optionally transitions color to PRESENT. Leaves CB open for picker / post.
    void record_main(const FrameRecordParams& params);

    // Color attachment → PRESENT_SRC (when post-process deferred the transition).
    void transition_color_to_present(VkCommandBuffer cmd, VkImage color_image);

    // vkEndCommandBuffer
    void end(VkCommandBuffer cmd);
};
