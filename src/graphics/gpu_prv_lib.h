#pragma once

#include "gpu_pub_lib.h"
#include "queue_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#define MAX_SWAPCHAIN_IMAGES ( 3 )

  //instance.cpp — app identity from host; engine identity from engine layer
VkInstance create_instance(const SoftwareIdentity& application,
                           const SoftwareIdentity& engine);
void destroy_instance(VkInstance instance);

  //validation.cpp
void create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance);

  //surface.cpp
VkSurfaceKHR create_win32_surface(VkInstance instance, void* hwnd, void* hinstance);
void destroy_surface(VkInstance instance, VkSurfaceKHR surface);

  //device.cpp
VkPhysicalDevice pick_physical_device(
    VkInstance instance,
    VkPhysicalDeviceProperties* device_props,
    VkPhysicalDeviceMemoryProperties* device_mem_props);

// Creates device with _3D / TX / CMP queues (see QueueType). surface used for present support.
void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkSurfaceKHR        surface,
    VkDevice           *device,
    DeviceQueues       *out_queues);
void destroy_device(VkDevice device);

uint32_t find_device_memory_type(
    VkPhysicalDeviceMemoryProperties* deviceMemProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties);

  //swapchain.cpp
void create_swapchain(
    VkDevice device,
    VkSurfaceKHR surface,
    VkSwapchainKHR* swapchain,
    std::vector<VkImage>& swapchainImages,
    VkFormat format,
    VkExtent2D extent);
void destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain);

  //buffer.cpp
void create_buffer(
    VkDevice                device, 
    VkPhysicalDeviceMemoryProperties
                           *deviceMemProps,
    VkDeviceSize            size,
    VkBufferUsageFlags      usage,
    VkMemoryPropertyFlags   properties,
    VkBuffer               &buffer,
    VkDeviceMemory         &memory);

void destroy_buffer(
    VkDevice                device,
    VkBuffer                buffer,
    VkDeviceMemory          memory);

  //image.cpp
void create_image(
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& image,
    VkDeviceMemory& imageMemory,
    VkPhysicalDeviceMemoryProperties* deviceMemProps);

void destroy_image(VkDevice device, VkImage image, VkDeviceMemory imageMemory);

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
void destroy_image_view(VkDevice device, VkImageView imageview);

  //shader.cpp
void load_shader_source(const char* const   filename,
    std::vector<uint8_t>& src);

void create_shader_module(VkDevice device, VkShaderModule &shaderModule, std::vector<uint8_t> &pCode );

void destroy_shader_module(VkDevice device, VkShaderModule shaderModule);

  //pipeline.cpp — reusable graphics pipeline helpers (dynamic rendering)
VkPipelineLayout create_pipeline_layout(
    VkDevice device,
    const VkDescriptorSetLayout* set_layouts,
    uint32_t set_layout_count,
    const VkPushConstantRange* push_ranges = nullptr,
    uint32_t push_count = 0);

void destroy_pipeline_layout(VkDevice device, VkPipelineLayout layout);
void destroy_pipeline(VkDevice device, VkPipeline pipeline);

enum class PipelineBlendMode : uint8_t
{
    None = 0,
    Alpha,           // src alpha / one-minus src alpha
    Additive,        // src alpha / one
    Premultiplied,   // one / one-minus src alpha (edge overlay)
};

struct GraphicsPipelineCreateInfo
{
    VkPipelineLayout layout = VK_NULL_HANDLE;

    const VkPipelineShaderStageCreateInfo* stages = nullptr;
    uint32_t stage_count = 0;

    const VkVertexInputBindingDescription* bindings = nullptr;
    uint32_t binding_count = 0;
    const VkVertexInputAttributeDescription* attributes = nullptr;
    uint32_t attribute_count = 0;

    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    float line_width = 1.0f;

    bool depth_test = true;
    bool depth_write = true;
    VkCompareOp depth_compare = VK_COMPARE_OP_LESS;

    PipelineBlendMode blend_mode = PipelineBlendMode::None;
    VkColorComponentFlags color_write_mask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    bool alpha_to_coverage = false;

    // Dynamic rendering
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    uint32_t color_attachment_count = 1; // 0 = depth-only

    // Optional fixed viewport/scissor (still usually dynamic for resize)
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;

    bool dynamic_viewport_scissor = true;
    bool dynamic_primitive_topology = false;
};

// Throws std::runtime_error on failure.
VkPipeline create_graphics_pipeline(VkDevice device, const GraphicsPipelineCreateInfo& info);

  //compute.cpp — reusable compute pipeline helpers
VkPipelineLayout create_compute_pipeline_layout(
    VkDevice device,
    const VkDescriptorSetLayout* set_layouts,
    uint32_t set_layout_count,
    const VkPushConstantRange* push_ranges = nullptr,
    uint32_t push_count = 0);

// Load SPV, create module, create pipeline, destroy module. Throws on failure.
VkPipeline create_compute_pipeline(
    VkDevice device,
    VkPipelineLayout layout,
    const char* shader_spv_path,
    const char* entry = "main");

VkPipeline create_compute_pipeline_from_module(
    VkDevice device,
    VkPipelineLayout layout,
    VkShaderModule module,
    const char* entry = "main");
