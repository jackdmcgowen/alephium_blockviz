#ifndef VULKAN_ENGINE_HPP
#define VULKAN_ENGINE_HPP

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
#include "engine/blockviz_engine_api.hpp"
#include "engine/frame_descriptors.hpp"
#include "engine/frame_recorder.hpp"
#include "engine/frame_resources.hpp"
#include "engine/frame_sync.hpp"
#include "engine/picker.hpp"
#include "engine/pipelines/cube_pipeline.hpp"
#include "engine/pipelines/picker_pipeline.hpp"
#include "engine/swapchain_targets.hpp"
#include "engine/vertex_types.hpp"
#include "graphics/gpu_pub_lib.h"
#include "graphics/gpu_prv_lib.h"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

#define NEAR_PLANE  ( 1.0f )
#define FAR_PLANE ( 5000.0f )

// Concrete engine: IRenderEngine + IBlockvizEngine (E6); FrameResources (E8).
class VulkanEngine : public IBlockvizEngine
{
public:
    using PushConstants = PickerPushConstants;

    VulkanEngine();
    ~VulkanEngine() override;

    // IRenderEngine
    void initialize(const EngineCreateInfo& info) override;
    void resize(uint32_t width, uint32_t height) override;
    void shutdown() override;
    void start() override;
    void stop() override;
    void submit_frame(const FrameSubmit& frame) override;
    void set_ui_overlay(IUiOverlay* overlay) override;
    void request_pick(const PickQuery& q) override;
    bool consume_pick(PickResult& out) override;

    // IBlockvizEngine
    void set_scene(BlockScene* scene) override;
    void set_camera(CameraState* camera) override;
    void set_selection(const std::string& hash) override;
    void clear_selection() override;
    bool is_selected(const std::string& hash) const override;
    AlphBlock copy_selected_block() const override;
    std::string consume_detail_refill_request() override;
    void publish_ui_snapshot(UiSnapshot snap) override;
    UiSnapshot copy_ui_snapshot() const override;
    void publish_frame(const FrameSubmit& frame,
                       const std::vector<std::string>& pick_map) override;
    void init_platform(void* hInstance, void* hwnd) override;
    void on_resize() override;

private:
    void resize_internal();
    void Resize(); // Win32 client-rect path
    static const int MAX_INSTANCES = 1024 * 1024;
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
    SwapchainTargets swapchain_targets_;
    FrameDescriptors frame_descriptors_;
    FrameRecorder frame_recorder_;

    CubePipeline cube_pipe_;
    PickerPipeline picker_pipe_;
    FrameResources frame_resources_;
    Picker picker_;

    VkCommandPool commandPool;
    FrameSync frame_sync_;

    enum class PickKind : uint8_t { None = 0, Click, Hover };

    struct FramesInFlight
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        bool            pendingPick = false;
        PickKind        pickKind = PickKind::None;
        std::vector<std::string> pick_map;
        uint64_t        pick_frame_seq = 0;
    } inFlightFrames[MAX_FRAMES_IN_FLIGHT];

    int currentFrame;
    size_t instanceCount = 0;

    std::thread renderThread;
    std::mutex  renderMutex;
    mutable std::mutex  selection_mutex_;

    BlockScene*   scene_   = nullptr;
    IUiOverlay*   overlay_ = nullptr;
    CameraState*  camera_  = nullptr;
    PolarShardLayout polar_layout_;

    std::string selected_hash_;
    AlphBlock   selected_block;
    std::string hovered_hash_;
    bool      has_look_target_ = false;
    glm::vec3 selected_look_pos_{ 0.f };
    glm::vec3 look_dir_{ 0.f, 0.f, 1.f };
    bool      look_dir_init_ = false;
    float     last_frame_dt_sec_ = 1.f / 60.f;
    std::string look_aim_hash_;
    glm::vec3   look_aim_dir_{ 0.f, 0.f, 1.f };
    bool        look_engaged_ = false;
    std::vector<std::string> pick_id_to_hash_;
    uint64_t  gpu_frame_seq_ = 0;

    mutable std::mutex detail_refill_mutex_;
    std::string        detail_refill_hash_;

    void set_selection_unlocked(const std::string& hash);
    void clear_selection_unlocked();
    void refresh_selection_if_needed(BlockScene& scene);
    void request_detail_refill_unlocked(const std::string& hash);
    void pin_and_maybe_refill(const std::string& hash, bool has_txns);

    bool running;
    float elapsedSeconds;
    uint32_t width;
    uint32_t height;

    void render_loop();
    void render();
    void create_swapchain_targets();
    void create_frame_resources();
    void create_frame_descriptors();
    void create_command_pool();
    void create_sync_objects();
    void update_uniform_buffer();
    glm::vec3 camera_eye() const;
    void begin_look_aim(const glm::vec3& eye, const glm::vec3& target_pos,
                        const std::string& hash);
    void end_look_aim();
    void update_look_direction(float dt, const glm::vec3& eye);
    CameraUBO build_camera_ubo() const;
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology);

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
    int pending_slot_ = -1;
    int reading_slot_ = -1;
    uint64_t submit_seq_ = 0;
    uint64_t ui_snap_seq_ = 0;

    bool apply_published_frame();
    int  find_free_gpu_slot_unlocked() const;

    mutable std::mutex ui_snap_mutex_;
    UiSnapshot ui_snap_;

    void cleanup();
};

#endif /* VULKAN_ENGINE_HPP */
