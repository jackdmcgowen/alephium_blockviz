#ifndef VULKAN_RENDERER_HPP
#define VULKAN_RENDERER_HPP

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <cstdint>

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "alph_block.hpp"
#include "app/camera_state.hpp"
#include "app/ui_snapshot.hpp"
#include "domain/layout.hpp"
#include "domain/block_scene.hpp"
#include "graphics/gpu_pub_lib.h"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

#define NEAR_PLANE  ( 1.0f )
#define FAR_PLANE ( 5000.0f )

// PR6b: engine has no cJSON ingest. Domain lives in BlockScene (adapter writes, renderer reads).
// PR7: triple-buffer FrameSubmit + UiSnapshot for race-free ImGui.
// PR8: engine hosts ImGui; chrome via IUiOverlay; camera via CameraState (no explorer URLs).
class VulkanRenderer
{
public:
    struct VertexNormal
    {
        glm::vec3 pos;
        glm::vec3 normal;
    };

    struct InstanceData
    {
        glm::vec3 pos;
        glm::vec3 color;
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

    struct PushConstants {
        uint32_t mouseX;
        uint32_t mouseY;
        uint32_t instanceOffset;
    };

    VulkanRenderer();
    ~VulkanRenderer();
    void Init(void *hInstance, void *hwnd);

    // Domain scene (owned by app; set before Start / network). Not owned.
    void set_scene(BlockScene* scene);

    // PR8: client chrome + camera (not owned; set before Start).
    void set_ui_overlay(IUiOverlay* overlay);
    void set_camera(CameraState* camera);

    // Thread-safe selection (network may call after replace).
    void set_selection(const std::string& hash);
    void clear_selection();
    bool is_selected(const std::string& hash) const;
    AlphBlock copy_selected_block() const;

    void Resize();
    void Start();
    void Stop();

    // PR6a/PR7: deep-copy into triple-buffer slot (latest-wins). Thread-safe.
    // Does not touch GPU; call apply_published_frame() on the render thread.
    void submit_frame(const FrameSubmit& frame, const std::vector<std::string>& pick_map);

    // PR7: publish overlay data (feed + selected detail). Thread-safe.
    void publish_ui_snapshot(UiSnapshot snap);
    UiSnapshot copy_ui_snapshot() const;

private:
    void resize();
    static const int MAX_INSTANCES = 1024*1024;
    static const VertexNormal CUBE_VERTICES[8];
    static const uint16_t CUBE_INDICES[36];

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
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkImage depthImage;
    VkFormat depthFormat;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkImage picker_image;
    VkImageView picker_imageView;
    VkDeviceMemory picker_memory;

    VkPipelineLayout picker_pipelineLayout;
    VkPipeline picker_pipeline;

    VkCommandPool commandPool;

    enum class PickKind : uint8_t { None = 0, Click, Hover };

    struct FramesInFlight
    {
        VkSemaphore     imageAvailableSemaphore;
        VkCommandBuffer commandBuffer;
        bool            pendingPick = false;
        PickKind        pickKind = PickKind::None;
        uint64_t        value = 0;
        std::vector<std::string> pick_map;
        uint64_t        pick_frame_seq = 0; // client_seq of GPU-visible frame when pick recorded
    } inFlightFrames[ MAX_FRAMES_IN_FLIGHT ];

    VkSemaphore     timeline;
    int currentFrame;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    VkBuffer instanceBuffer;
    VkDeviceMemory instanceBufferMemory;
    void* mappedInstanceMemory;
    size_t instanceCount;
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    std::thread renderThread;
    std::mutex  renderMutex;
    mutable std::mutex  selection_mutex_; // selection only; scene has its own mutex

    BlockScene*   scene_   = nullptr;
    IUiOverlay*   overlay_ = nullptr;
    CameraState*  camera_  = nullptr;
    PolarShardLayout polar_layout_;

    std::string selected_hash_;
    AlphBlock   selected_block;
    std::string hovered_hash_;
    bool      has_look_target_ = false;
    glm::vec3 selected_look_pos_{ 0.f };
    std::vector<std::string> pick_id_to_hash_;
    uint64_t  gpu_frame_seq_ = 0; // client_seq of currently applied GPU frame

    void set_selection_unlocked(const std::string& hash);
    void clear_selection_unlocked();
    void refresh_selection_if_needed(BlockScene& scene);

    bool running;
    float elapsedSeconds;
    uint32_t width;
    uint32_t height;

    void render_loop();

    void render();
    void create_depth_resources();
    void create_image_views();
    void create_descriptor_set_layout();
    void create_graphics_pipeline();
    void create_picker_resources();
    void create_picker_pipeline();
    void create_vertex_buffer();
    void create_index_buffer();
    void create_instance_buffer();
    void create_uniform_buffer();
    void create_descriptor_pool();
    void create_descriptor_sets();
    void create_command_pool();
    void create_sync_objects();
    void update_uniform_buffer();
    CameraUBO build_camera_ubo() const;
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology);

    // PR7 triple-buffer: writing free slot → pending; render acquires pending → reading
    static constexpr int kGpuSlots = 3;
    struct GpuFrameSlot
    {
        std::vector<GpuInstance> instances;
        CameraUBO camera{};
        uint64_t client_seq = 0;
        std::vector<std::string> pick_map;
    };
    GpuFrameSlot gpu_slots_[kGpuSlots];
    mutable std::mutex submit_mutex_;
    int pending_slot_ = -1;  // latest published, not yet acquired (-1 = none)
    int reading_slot_ = -1;  // currently GPU-visible on render thread
    uint64_t submit_seq_ = 0;
    uint64_t ui_snap_seq_ = 0;

    // Acquire pending → reading and upload instances/camera to GPU. Render thread only.
    bool apply_published_frame();
    int  find_free_gpu_slot_unlocked() const;

    // UiSnapshot publish (separate from GPU slots)
    mutable std::mutex ui_snap_mutex_;
    UiSnapshot ui_snap_;

    void record_picker_pass(VkCommandBuffer buffer, uint32_t mouseX, uint32_t mouseY, uint32_t instanceOffset = 0);
    uint32_t read_picker_obj_id(VkDevice device);

    VkFormat find_depth_format();
    void cleanup();
};

#endif /* VULKAN_RENDERER_HPP */
