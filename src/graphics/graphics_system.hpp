#ifndef GRAPHICS_SYSTEM_HPP
#define GRAPHICS_SYSTEM_HPP

// Internal to graphics.lib only (create_graphics_system returns IGraphicsSystem*).
// Host/app code must not include this header — keeps Vulkan out of product TUs.
//
// Method bodies are split across TUs (still GraphicsSystem::):
//   graphics_system.cpp     — lifecycle, init/resize, create_*, record_command_buffer
//   frame/frame_loop.cpp    — render_loop / render (outline_count → async Sobel)
//   frame/gpu_frame_publish.cpp  — publish_frame / apply_published_frame
//   frame/selection_state.cpp    — selection, hover, multi-tx filter, detail refill
// Sobel: frame/passes/* IPass nodes + frame/sobel_async_pass.* (not GS methods)
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
#include "graphics/frame/frame_resources.hpp"
#include "graphics/frame/frame_sync.hpp"
#include "graphics/frame/passes/main_scene_pass.hpp"
#include "graphics/frame/passes/picker_pass.hpp"
#include "graphics/frame/passes/sobel_resources.hpp"
#include "graphics/frame/passes/outline_pass.hpp"
#include "graphics/frame/passes/sobel_compute_pass.hpp"
#include "graphics/frame/passes/edge_overlay_pass.hpp"
#include "graphics/frame/sobel_async_pass.hpp"
#include "graphics/frame/sobel_types.hpp"
#include "graphics/frame/swapchain_targets.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/buffer_manager.hpp"
#include "graphics/engine_requirements.hpp"
#include "graphics/gpu_pub_lib.h"
#include "graphics/gpu_prv_lib.h"
#include "graphics/core/queue_types.hpp"
#include "graphics/core/sampler.hpp"

#define MAX_FRAMES_IN_FLIGHT ( 3 )
#define WDW_WIDTH  1024
#define WDW_HEIGHT 1024

// Graphics backend (GPU + render thread). Product shell is BlockVizEngine.
class GraphicsSystem : public IGraphicsSystem
{
public:
    using PushConstants = ::PickerPushConstants;

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
    void set_scene_view_filters(const SceneViewFilters& filters) override;
    SceneViewFilters scene_view_filters() const override;
    // Capture client area (scene + ImGui) to PNG. Empty path → docs/images/capture_*.png
    void request_screenshot(const char* path_utf8) override;
    std::string consume_detail_refill_request() override;
    void publish_ui_snapshot(UiSnapshot snap) override;
    UiSnapshot copy_ui_snapshot() const override;
    void publish_frame(const FrameSubmit& frame,
                       const std::vector<std::string>& pick_map,
                       const std::vector<SobelOutlineInstance>& sobel_outlines) override;
    void init_platform(void* hInstance, void* hwnd) override;
    void on_resize() override;

    void enable_frame_profiler(bool enabled) override;
    bool frame_profiler_enabled() const override;
    void copy_frame_timing_snapshot(FrameTimingSnapshot& out) const override;

    // Kill-switch for role outlines (tips/cyan/orange). Selection gold still emitted by app.
    void set_visualize_confirmed_tips(bool enabled) { visualize_confirmed_tips_ = enabled; }
    bool visualize_confirmed_tips() const { return visualize_confirmed_tips_; }

    // Render-thread access for record/Sobel instrumentation.
    FrameProfiler* frame_profiler() { return &frame_profiler_; }

    // Optional device capabilities (mesh shaders, etc.). Queried at init; not enabled yet.
    const DeviceOptionalFeatures& device_optional_features() const
    {
        return device_optional_features_;
    }

private:
    void resize_internal();
    void Resize(); // Win32 client-rect path
    static const int MAX_INSTANCES = 1024 * 1024;
    static const VertexNormal CUBE_VERTICES[8];
    static const uint16_t CUBE_INDICES[36];

    void *hInstance;
    void *hwnd;
    bool headless_ = false;
    uint32_t last_swapchain_image_index_ = 0;
    bool last_swapchain_image_valid_ = false;

    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceMemoryProperties deviceMemProps;
    DeviceOptionalFeatures device_optional_features_{};
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
    FramePresenter frame_presenter_;

    SamplerTable sampler_table_;
    frame_graph::MainScenePass main_scene_pass_;
    frame_graph::PickerPass picker_pass_;
    FrameResources frame_resources_;
    frame_graph::SobelResources sobel_resources_;
    frame_graph::OutlinePass outline_pass_;
    frame_graph::SobelComputePass sobel_compute_pass_;
    frame_graph::EdgeOverlayPass edge_overlay_pass_;
    SobelAsyncPass sobel_async_;

    VkCommandPool commandPool = VK_NULL_HANDLE;       // _3D family
    VkCommandPool computeCommandPool = VK_NULL_HANDLE; // CMP family
    FrameSync frame_sync_;

    enum class PickKind : uint8_t { None = 0, Click, Hover };

    struct FramesInFlight
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // main scene + pick + outline depth/color
        VkCommandBuffer computeCommandBuffer = VK_NULL_HANDLE; // async Sobel CMP
        VkCommandBuffer overlayCommandBuffer = VK_NULL_HANDLE; // edge overlay (+ present last)
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
    SceneViewFilters scene_view_filters_{};
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
    // when capture_screenshot: skip present transition in main; insert copy+present after draws
    void record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex,
                               VkPrimitiveTopology topology, bool defer_present,
                               bool capture_screenshot = false);

    static constexpr int kGpuSlots = 3;
    struct GpuFrameSlot
    {
        std::vector<GpuInstance> instances;
        CameraUBO camera{};
        uint64_t client_seq = 0;
        std::vector<std::string> pick_map;
        std::vector<SobelOutlineInstance> sobel_outlines;
    };
    GpuFrameSlot gpu_slots_[kGpuSlots];
    mutable std::mutex submit_mutex_;
    int pending_slot_ = -1;
    int reading_slot_ = -1;
    uint64_t submit_seq_ = 0;
    uint64_t ui_snap_seq_ = 0;

    bool apply_published_frame();
    int  find_free_gpu_slot_unlocked() const;

    // Kill-switch: gates role outlines via FrameSourceInput; selection gold always works.
    bool visualize_confirmed_tips_ = true;

    FrameProfiler frame_profiler_;
    bool          profiler_hud_ = false; // F3 toggles HUD; enabling profiler shows data

    mutable std::mutex ui_snap_mutex_;
    UiSnapshot ui_snap_;

    EngineCreateInfo create_info_{};
    bool configured_ = false;
    bool inited_ = false;

    void cleanup();

    // Screenshot (GPU readback while swapchain image is still acquired). Render thread only.
    mutable std::mutex screenshot_mutex_;
    std::string        screenshot_pending_path_;
    // Filled when this frame will capture; PNG written after submit completes.
    std::string        screenshot_path_this_frame_;
    bool               screenshot_copy_recorded_ = false;
    VkBuffer           screenshot_staging_ = VK_NULL_HANDLE;
    VkDeviceMemory     screenshot_staging_mem_ = VK_NULL_HANDLE;
    VkDeviceSize       screenshot_staging_size_ = 0;
    uint32_t           screenshot_extent_w_ = 0;
    uint32_t           screenshot_extent_h_ = 0;

    bool begin_screenshot_frame_(); // claim pending path for this frame
    void ensure_screenshot_staging_();
    void destroy_screenshot_staging_();
    // COLOR_ATTACHMENT → TRANSFER_SRC → copy → PRESENT (image must still be acquired).
    void record_screenshot_before_present_(VkCommandBuffer cmd, VkImage swapchain_image);
    void finish_screenshot_after_submit_();
};

#endif /* GRAPHICS_SYSTEM_HPP */
