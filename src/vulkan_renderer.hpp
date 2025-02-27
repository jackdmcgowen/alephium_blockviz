#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "dag.hpp"

#define MAX_LINES 10000 // Arbitrary cap for now
#define WDW_WIDTH 800
#define WDW_HEIGHT 600

#define NEAR_PLANE  ( 1.0f )
#define FAR_PLANE ( 200.0f )

class VulkanRenderer
{
public:
    struct Vertex
    {
        float x, y, z; // Position
        float r, g, b; // Color
    };

    struct UniformBufferObject
    {
        glm::mat4 view;
        glm::mat4 proj;
    };

    VulkanRenderer();
    ~VulkanRenderer();
    void init(HINSTANCE hInstance, HWND hwnd);
    void add_block(cJSON* block);
    void start();
    void stop();

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
    VkDescriptorSetLayout descriptorSetLayout;
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
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    std::thread renderThread;
    std::mutex  dataMutex;
    std::condition_variable dataCond;
    std::queue<cJSON*> blockQueue;
    bool running;
    std::vector<Vertex> vertices;
    float timeOffset;

    void render_loop();

    void render();
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void create_image_views();
    void create_render_pass();
    void create_descriptor_set_layout();
    void create_graphics_pipeline();
    void create_framebuffers();
    void create_vertex_buffer();
    void create_uniform_buffer();
    void create_descriptor_pool();
    void create_descriptor_sets();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void update_uniform_buffer();
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex );
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void cleanup();
};

#endif /* VULKAN_RENDERER_HPP */
