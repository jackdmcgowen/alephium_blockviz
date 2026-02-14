#include <vulkan/vulkan.h>

#include <stdexcept>
#include <vector>

  //instance.cpp
VkInstance create_instance();
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

void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkDevice           *device,
    VkQueue            *queue);
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
void create_shader_module(VkDevice device, VkShaderModule &shaderModule, std::vector<uint8_t> &pCode );

void destroy_shader_module(VkDevice device, VkShaderModule shaderModule);
