#ifndef GRAPHICS_SYSTEM_HPP
#define GRAPHICS_SYSTEM_HPP

// Internal to graphics.lib only (create_graphics_system returns IGraphicsSystem*).
// Host/app code must not include this header — keeps Vulkan out of product TUs.
//
// Method bodies are split across TUs (still GraphicsSystem::):
//   graphics_system.cpp     — lifecycle, init/resize, create_*, record_command_buffer
//   frame/frame_loop.cpp    — render_loop / render (builds SobelFrameRequest)
//   frame/gpu_frame_publish.cpp  — publish_frame / apply_published_frame
//   frame/selection_state.cpp    — selection, hover, multi-tx filter, detail refill
// Sobel: pipelines/sobel_pipeline.* + frame/sobel_async_pass.* (not GS methods)
// See docs/layers/graphics.md (living module map).

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
#include "domain/alph_block.hpp"
#include "app/camera_controller.hpp"
#include "app/ui_snapshot.hpp"
#include "domain/block_scene.hpp"
#include "engine/engine.hpp"
#include "graphics/frame/frame_descriptors.hpp"
#include "graphics/frame/frame_presenter.hpp"
#include "graphics/frame/frame_recorder.hpp"
#include "graphics/frame/frame_resources.hpp"
#include "graphics/frame/frame_sync.hpp"
#include "graphics/frame/picker.hpp"
#include "graphics/pipelines/cube_pipeline.hpp"
#include "graphics/pipelines/picker_pipeline.hpp"
#include "graphics/pipelines/sobel_pipeline.hpp"
#include "graphics/frame/sobel_async_pass.hpp"
#include "graphics/frame/sobel_types.hpp"
#include "graphics/frame/swapchain_targets.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/buffer_manager.hpp"
#include "graphics/gpu_pub_lib.h"
#include "graphics/gpu_prv_lib.h"
#include "graphics/queue_types.hpp"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

// Graphics backend (GPU + render thread). Product shell is BlockVizEngine.
class GraphicsSystem : public IGraphicsSystem
{
public:
    using PushConstants = PickerPushConstants;

    GraphicsSystem();
    ~GraphicsSystem() override;

    // ISystem
    const char* name() const override { return "GraphicsSystem"; }
    void init() override;
    void free() override;
    void start() override;
    void stop() override;

    // IGraphicsSystem
    void configure(const EngineCreateInfo& info) override;
    void resize(uint32_t width, uint32_t height) override;
    void submit_frame(const FrameSubmit& frame) override;
    void set_ui_overlay(IUiOverlay* overlay) override;
    void request_pick(const PickQuery& q) override;
    bool consume_pick(PickResult& out) override;

    void set_scene(BlockScene* scene) override;
    void set_camera(CameraController* camera) override;
    void set_frame_source(IFrameSource* source) override;
    void set_selection(const std::string& hash) override;
    void clear_selection() override;
    bool is_selected(const std::string& hash) const override;
    AlphBlock copy_selected_block() const override;
    void set_ui_dep_hover(const std::string& hash) override;
    void set_scene_filter_multi_tx(bool enabled) override;
    void set_scene_filter_min_alph(double min_alph) override;
    // Capture client area (scene + ImGui) to PNG. Empty path → docs/images/capture_*.png
    void request_screenshot(const char* path_utf8) override;
    std::string consume_detail_refill_request() override;
    void publish_ui_snapshot(UiSnapshot snap) override;
    UiSnapshot copy_ui_snapshot() const override;
    void publish_frame(const FrameSubmit& frame,
                       const std::vector<std::string>& pick_map,
                       const std::vector<std::string>& confirmed_tip_hashes,
                       const std::vector<std::string>& cyan_frontier_hashes,
                       const std::vector<std::string>& incomplete_hashes) override;
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
    SobelPipeline  sobel_pipe_;
    SobelAsyncPass sobel_async_;

    VkCommandPool commandPool = VK_NULL_HANDLE;       // _3D family
    VkCommandPool computeCommandPool = VK_NULL_HANDLE; // CMP family
    FrameSync frame_sync_;

    enum class PickKind : uint8_t { None = 0, Click, Hover };

    struct FramesInFlight
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // main scene + pick + first sel depth
        VkCommandBuffer computeCommandBuffer = VK_NULL_HANDLE; // async Sobel CMP
        VkCommandBuffer overlayCommandBuffer = VK_NULL_HANDLE; // edge overlay (+ present last)
        VkCommandBuffer layerDepthCommandBuffer = VK_NULL_HANDLE; // extra Sobel layers depth
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
    std::string ui_dep_hover_hash_; // inspector Deps row hover
    bool        filter_multi_tx_ = false; // scene: txn_count > 1 only
    double      filter_min_alph_ = 0.0;   // human ALPH; 0 = off
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

    static constexpr int kGpuSlots = 3;
    struct GpuFrameSlot
    {
        std::vector<GpuInstance> instances;
        CameraUBO camera{};
        uint64_t client_seq = 0;
        std::vector<std::string> pick_map;
        std::vector<std::string> confirmed_tip_hashes;
        std::vector<std::string> cyan_frontier_hashes;
        std::vector<std::string> incomplete_hashes;
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
    std::vector<std::string> sobel_cyan_hashes_;
    std::vector<std::string> sobel_incomplete_hashes_;
    // Kill-switch: gates tip/incomplete Sobel; selection gold always works. Default on.
    bool visualize_confirmed_tips_ = true;

    mutable std::mutex ui_snap_mutex_;
    UiSnapshot ui_snap_;

    EngineCreateInfo create_info_{};
    bool configured_ = false;
    bool inited_ = false;

    void cleanup();

    // Screenshot (Win32 client capture → PNG). Processed on render thread.
    mutable std::mutex screenshot_mutex_;
    std::string        screenshot_pending_path_;
    bool consume_and_save_screenshot_();
};

#endif /* GRAPHICS_SYSTEM_HPP */
