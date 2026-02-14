#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "alph_block.hpp"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

#define NEAR_PLANE  ( 1.0f )
#define FAR_PLANE ( 5000.0f )

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
        glm::float32 pad1;
        glm::vec3 viewPos;
        glm::float32 pad2;
        glm::float32 meters;
    };

    VulkanRenderer();
    ~VulkanRenderer();
    void Init(void *hInstance, void *hwnd);
    void Add_Block(cJSON* block);
    void Resize();
    void Start();
    void Stop();

private:
    void resize();
    static const int MAX_INSTANCES = 1024*1024; // Arbitrary large size
    static const Vertex CUBE_VERTICES[8]; // 8 corners
    static const uint16_t CUBE_INDICES[36]; // 12 triangles

    void *hInstance;
    void *hwnd;

    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceMemoryProperties deviceMemProps;
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

    struct FramesInFlight
    {
        VkSemaphore     renderFinishedSemaphore;
        VkSemaphore     imageAvailableSemaphore;
        VkCommandBuffer commandBuffer;
        VkFence         fence;
    } inFlightFrames[ MAX_FRAMES_IN_FLIGHT ];

    int currentFrame;
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

    std::thread renderThread;
    std::mutex  renderMutex;
    std::mutex  dataMutex;
    std::condition_variable dataCond;

    using HashToBlocks = std::map<std::string, AlphBlock>;
    using HeightToHash = std::map<uint64_t, HashToBlocks>;
    std::vector<HeightToHash> chains;


    std::deque<AlphBlock> blockQueue;
    int total_blocks;
    bool running;
    float elapsedSeconds;
    uint32_t width;
    uint32_t height;

    void render_loop();

    void render();
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
    void create_sync_objects();
    void update_uniform_buffer();
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology);
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    VkFormat find_depth_format();
    void cleanup();
};

#endif /* VULKAN_RENDERER_HPP */
