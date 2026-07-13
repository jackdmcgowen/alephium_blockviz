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
#include "app/camera_controller.hpp"
#include "app/ui_snapshot.hpp"
#include "domain/block_scene.hpp"
#include "engine/blockviz_engine_api.hpp"
#include "engine/frame_descriptors.hpp"
#include "engine/frame_presenter.hpp"
#include "engine/frame_recorder.hpp"
#include "engine/frame_resources.hpp"
#include "engine/frame_sync.hpp"
#include "engine/picker.hpp"
#include "engine/pipelines/cube_pipeline.hpp"
#include "engine/pipelines/picker_pipeline.hpp"
#include "engine/sobel_compute.hpp"
#include "engine/swapchain_targets.hpp"
#include "engine/vertex_types.hpp"
#include "graphics/buffer_manager.hpp"
#include "graphics/gpu_pub_lib.h"
#include "graphics/gpu_prv_lib.h"
#include "graphics/queue_types.hpp"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

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
    void set_camera(CameraController* camera) override;
    void set_frame_source(IFrameSource* source) override;
    void set_selection(const std::string& hash) override;
    void clear_selection() override;
    bool is_selected(const std::string& hash) const override;
    AlphBlock copy_selected_block() const override;
    std::string consume_detail_refill_request() override;
    void publish_ui_snapshot(UiSnapshot snap) override;
    UiSnapshot copy_ui_snapshot() const override;
    void publish_frame(const FrameSubmit& frame,
                       const std::vector<std::string>& pick_map,
                       const std::vector<std::string>& confirmed_tip_hashes) override;
    void init_platform(void* hInstance, void* hwnd) override;
    void on_resize() override;

    // Kill-switch for always-on confirmed-tip green Sobel (default on). Selection gold unaffected.
    void set_visualize_confirmed_tips(bool enabled) { visualize_confirmed_tips_ = enabled; }
    bool visualize_confirmed_tips() const { return visualize_confirmed_tips_; }

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
    DeviceQueues queues_{}; // indexed by QueueType {_3D, TX, CMP}
    BufferManager buffer_manager_;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    SwapchainTargets swapchain_targets_;
    FrameDescriptors frame_descriptors_;
    FrameRecorder frame_recorder_;
    FramePresenter frame_presenter_;

    CubePipeline cube_pipe_;
    PickerPipeline picker_pipe_;
    FrameResources frame_resources_;
    Picker picker_;
    SobelCompute sobel_;

    VkCommandPool commandPool = VK_NULL_HANDLE;       // _3D family
    VkCommandPool computeCommandPool = VK_NULL_HANDLE; // CMP family
    // Serializes async Sobel so shared sel_depth/edge are not rewritten mid-flight (VUID-09600).
    VkFence sobel_done_fence_ = VK_NULL_HANDLE;
    bool    sobel_fence_in_flight_ = false;
    FrameSync frame_sync_;

    enum class PickKind : uint8_t { None = 0, Click, Hover };

    struct FramesInFlight
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // main scene + pick + sel depth
        VkCommandBuffer computeCommandBuffer = VK_NULL_HANDLE; // async Sobel CMP
        VkCommandBuffer overlayCommandBuffer = VK_NULL_HANDLE; // edge overlay + present
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

    BlockScene*       scene_        = nullptr;
    IUiOverlay*       overlay_      = nullptr;
    CameraController* camera_       = nullptr;
    IFrameSource*     frame_source_ = nullptr;

    std::string selected_hash_;
    AlphBlock   selected_block;
    std::string hovered_hash_;
    float     last_frame_dt_sec_ = 1.f / 60.f;
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
    uint32_t width;
    uint32_t height;

    void render_loop();
    void render();
    void create_swapchain_targets();
    void create_frame_resources();
    void create_frame_descriptors();
    void create_command_pool();
    void create_sync_objects();
    // when defer_present: leave color as attachment for async Sobel overlay
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex,
                               VkPrimitiveTopology topology, bool defer_present);

    // Mode + instance indices for async Sobel (gold selection or green confirmed tips).
    struct SobelFrameRequest
    {
        enum class Mode { SelectionGold, ConfirmedTipsGreen } mode = Mode::SelectionGold;
        std::vector<uint32_t> instance_indices; // size 1 for selection; 1..32 for tips
    };

    void submit_frame_with_async_sobel(uint32_t frame_index, uint32_t image_index,
                                       VkCommandBuffer graphics_cb,
                                       VkSemaphore image_available,
                                       VkSemaphore render_finished,
                                       const SobelFrameRequest& req);

    static constexpr int kGpuSlots = 3;
    struct GpuFrameSlot
    {
        std::vector<GpuInstance> instances;
        CameraUBO camera{};
        uint64_t client_seq = 0;
        std::vector<std::string> pick_map;
        std::vector<std::string> confirmed_tip_hashes;
    };
    GpuFrameSlot gpu_slots_[kGpuSlots];
    mutable std::mutex submit_mutex_;
    int pending_slot_ = -1;
    int reading_slot_ = -1;
    uint64_t submit_seq_ = 0;
    uint64_t ui_snap_seq_ = 0;

    bool apply_published_frame();
    int  find_free_gpu_slot_unlocked() const;

    // Loaded from GpuFrameSlot in apply_published_frame (paired with pick_id_to_hash_).
    std::vector<std::string> sobel_tip_hashes_;
    // Kill-switch: gates ConfirmedTipsGreen only; selection gold always works. Default on (K8).
    bool visualize_confirmed_tips_ = true;

    mutable std::mutex ui_snap_mutex_;
    UiSnapshot ui_snap_;

    void cleanup();
};

#endif /* VULKAN_ENGINE_HPP */
