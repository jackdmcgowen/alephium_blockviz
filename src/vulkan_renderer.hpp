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
#include <set>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "alph_block.hpp"

#define WDW_WIDTH  1920
#define WDW_HEIGHT 1080

#define NEAR_PLANE  ( 1.0f )
#define FAR_PLANE ( 1000.0f )

class VulkanRenderer
{
public:
    struct Vertex
    {
        glm::vec3 pos;    // Vertex position
        glm::vec3 normal; // Vertex normal
    };

    struct InstanceData
    {
        glm::vec3 pos; // Block position
        glm::vec3 color; //Block colors
    };

    struct UniformBufferObject
    {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 lightPos;
        glm::vec3 viewPos;
    };

    VulkanRenderer();
    ~VulkanRenderer();
    void init(HINSTANCE hInstance, HWND hwnd);
    void add_block(cJSON* block);
    void start();
    void stop();

private:
    static const int MAX_INSTANCES = 1024*1024; // Arbitrary large size
    static const Vertex CUBE_VERTICES[8]; // 8 corners
    static const uint16_t CUBE_INDICES[36]; // 12 triangles

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
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
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
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    VkBuffer instanceBuffer;
    VkDeviceMemory instanceBufferMemory;
    void* mappedInstanceMemory; // Persistent mapping
    size_t instanceCount;
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkDebugUtilsMessengerEXT debugMessenger;

    std::thread renderThread;
    std::mutex  dataMutex;
    std::condition_variable dataCond;
    std::set<AlphBlock> blockSet;
    bool running;
    float timeOffset;

    void render_loop();

    void render();
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void create_depth_resources();
    void create_image_views();
    void create_render_pass();
    void create_descriptor_set_layout();
    void create_graphics_pipeline();
    void create_framebuffers();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_instance_buffer();
    void create_uniform_buffer();
    void create_descriptor_pool();
    void create_descriptor_sets();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();
    void update_uniform_buffer();
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology);
    uint32_t find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    VkFormat find_depth_format();
    void cleanup();
    void setup_debug_messenger();
};

#endif /* VULKAN_RENDERER_HPP */
