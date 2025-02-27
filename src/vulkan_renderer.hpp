#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

#include "dag.hpp"

#define MAX_LINES 10000 // Arbitrary cap for now
#define WDW_WIDTH 800
#define WDW_HEIGHT 600

class VulkanRenderer
{
public:
    struct Vertex
    {
        float x, y, z; // Position
        float r, g, b; // Color
    };

    VulkanRenderer();
    ~VulkanRenderer();
    void init(HINSTANCE hInstance, HWND hwnd);
    void update(const Dag& dag);
    void render();
    void destroy();

private:
    HINSTANCE hInstance;
    HWND hwnd;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void create_image_views();
    void create_render_pass();
    void create_graphics_pipeline();
    void create_framebuffers();
    void create_vertex_buffer(std::vector<Vertex>& vertices);
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, uint32_t vertexCount);
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
};

#endif /* VULKAN_RENDERER_HPP */
