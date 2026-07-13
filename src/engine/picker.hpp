#pragma once

// GPU pick resources + record pass + CPU readback (E10).
// Pick policy (when to pick, hash maps, selection) stays on GraphicsSystem.
#include "engine/vertex_types.hpp"
#include "graphics/buffer_manager.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

inline constexpr VkFormat kPickerColorFormat = VK_FORMAT_R32_UINT;
inline constexpr uint32_t kPickerInvalidId   = ~0u;
inline constexpr VkExtent2D kPickerReadExtent = { 1, 1 };

struct PickerResourcesCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    // Always 1× depth for pick (must not share MSAA scene depth).
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
};

struct PickerRecordParams
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t mouse_x = 0;
    uint32_t mouse_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VkExtent2D viewport_extent{ 0, 0 };
    bool image_layout_undefined = false; // true after resize / first use

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    uint32_t instance_count = 0;
    uint32_t instance_offset = 0;
    uint32_t index_count = 36;
};

class Picker
{
public:
    void create_resources(const PickerResourcesCreateInfo& info);
    void destroy_resources(VkDevice device);

    void recreate_resources(const PickerResourcesCreateInfo& info);

    void create_staging(BufferManager* buffers);
    void destroy_staging();

    void destroy(VkDevice device); // resources + staging

    void record_pass(const PickerRecordParams& params);
    // Returns kPickerInvalidId on miss / clear id.
    uint32_t read_object_id(VkDevice device) const;

    VkFormat color_format() const { return kPickerColorFormat; }

private:
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

    // Private 1× depth — never the MSAA scene depth (validation: sample counts must match).
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;

    BufferManager* buffers_ = nullptr;
    GpuBuffer staging_;
};
