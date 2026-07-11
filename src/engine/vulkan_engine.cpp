#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>
#include "engine/vulkan_engine.hpp"
#include "engine/engine_identity.hpp"
#include "app/ui_chrome.hpp"
#include "commands.h"
#include "graphics/engine_requirements.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "graphics/debug/debug_drawer.h"
#include "graphics/mesh_arena.h"

#include "gpu_prv_lib.h"


#include "imgui.h"
#include "imgui_impl_win32.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include "imgui_impl_vulkan.h"
#include <windows.h>

#include <vulkan/vulkan_win32.h>
#include <vulkan/vk_enum_string_helper.h>


static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;

    fprintf(stderr, "[vulkan] Error: VkResult = %s\n", string_VkResult(err) );
    if (err < 0)
        abort();

}   /* check_vk_result() */


static DebugDrawer debugDrawer;
static MeshArena* meshArena = nullptr;
glm::mat4 viewProj;

static const float FOV = glm::radians(45.0f);

// Shorten a segment so arrow ends sit outside cube centers (clearance in world units).
static bool inset_segment(const glm::vec3& from, const glm::vec3& to, float clearance,
                          glm::vec3& from_out, glm::vec3& to_out)
{
    const glm::vec3 delta = to - from;
    const float len = glm::length(delta);
    if (len < 2.0f * clearance + 1e-4f)
        return false;
    const glm::vec3 dir = delta / len;
    from_out = from + dir * clearance;
    to_out   = to   - dir * clearance;
    return true;
}

// Active frontier BlockFlow edges (not per-shard muted colors)
static const glm::vec4 kActiveArrowColor(0.15f, 0.95f, 1.0f, 0.95f);
// Selection dependency edges
static const glm::vec4 kSelectionArrowColor(1.0f, 0.85f, 0.2f, 1.0f);
// Hover preview deps (dimmer gold)
static const glm::vec4 kHoverArrowColor(1.0f, 0.85f, 0.2f, 0.45f);

static constexpr bool kHoverPickEnabled = true;

const VertexNormal VulkanEngine::CUBE_VERTICES[8] = {
    { glm::vec3(-1, -1,  1), glm::normalize(glm::vec3(-1, -1,  1)) }, // 0
    { glm::vec3(1, -1,  1),  glm::normalize(glm::vec3(1, -1,  1)) }, // 1
    { glm::vec3(-1, -1, -1), glm::normalize(glm::vec3(-1, -1, -1)) }, // 2
    { glm::vec3(-1,  1, -1), glm::normalize(glm::vec3(-1,  1, -1)) }, // 3
    { glm::vec3(-1,  1,  1), glm::normalize(glm::vec3(-1,  1,  1)) }, // 4
    { glm::vec3(1,  1, -1),  glm::normalize(glm::vec3(1,  1, -1)) }, // 5
    { glm::vec3(1, -1, -1),  glm::normalize(glm::vec3(1, -1, -1)) }, // 6
    { glm::vec3(1,  1,  1),  glm::normalize(glm::vec3(1,  1,  1)) }  // 7
};

const uint16_t VulkanEngine::CUBE_INDICES[36] = {
    6, 0, 2, 6, 1, 0,
    4, 0, 1, 0, 4, 3,
    0, 3, 2, 3, 5, 2,
    5, 6, 2, 6, 5, 7,
    6, 7, 1, 1, 7, 4,
    3, 4, 7, 7, 5, 3
};

static bool s_resized;

// Layout spacing (not camera; still engine-side until layout owns params fully)
static float meters_per_height = ALPH_TARGET_BLOCK_SECONDS;

// Camera rotate-into (selection) / rotate-home (right-click clear)
static constexpr float kLookOmega = 10.0f; // slerp ease rate (higher = snappier)
static const glm::vec3 kCamUp(0.0f, -1.0f, 0.0f);
static const glm::vec3 kCamForward(0.0f, 0.0f, 1.0f); // original free view

static void pipeline_barrier(VkCommandBuffer buffer, VkImage image,
    VkImageLayout oldLayout, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 srcStageMask,
    VkImageLayout newLayout, VkAccessFlags2 dstAccessMask, VkPipelineStageFlags2 dstStageMask,
    VkImageSubresourceRange subresourceRange
)
{

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcAccessMask = srcAccessMask;
    barrier.srcStageMask = srcStageMask;

    barrier.dstAccessMask = dstAccessMask;
    barrier.dstStageMask = dstStageMask;

    barrier.oldLayout = oldLayout; // or UNDEFINED first time
    barrier.newLayout = newLayout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(buffer, &depInfo);

}   /* pipeline_barrier() */

VulkanEngine::VulkanEngine()
    : hInstance(nullptr)
    , hwnd(nullptr)
    , instance(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
    , deviceProps()
    , deviceMemProps()
    , device(VK_NULL_HANDLE)
    , graphicsQueue(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , swapchain(VK_NULL_HANDLE)
    , descriptorSetLayout(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , inFlightFrames{}
    , currentFrame(0)
    , instanceCount(0)
    , descriptorPool(VK_NULL_HANDLE)
    , descriptorSet(VK_NULL_HANDLE)
    , running(false)
    , elapsedSeconds(0.0f)
    , width(0)
    , height(0)
{
}   /* VulkanEngine() */

void VulkanEngine::set_scene(BlockScene* scene)
{
    scene_ = scene;
}

void VulkanEngine::set_ui_overlay(IUiOverlay* overlay)
{
    overlay_ = overlay;
}

void VulkanEngine::set_camera(CameraState* camera)
{
    camera_ = camera;
}

void VulkanEngine::resize(uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return;
    std::lock_guard<std::mutex> lk(renderMutex);
    if (w == width && h == height)
        return;
    width = w;
    height = h;
    resize_internal();
}

void VulkanEngine::on_resize()
{
    Resize(); // reads client rect then resize_internal
}

void VulkanEngine::shutdown()
{
    stop();
    cleanup();
}

void VulkanEngine::request_pick(const PickQuery& /*q*/)
{
    // Picks are driven from the render thread mouse path for now.
}

bool VulkanEngine::consume_pick(PickResult& out)
{
    out = PickResult{};
    return false;
}

VulkanEngine::~VulkanEngine()
{
    stop();
    cleanup();
}

void VulkanEngine::init_platform(void* hInst, void* hwnd_)
{
    // Prefer host calling initialize() with application identity filled in.
    EngineCreateInfo info{};
    info.platform_instance = hInst;
    info.window = hwnd_;
    initialize(info);
}

void VulkanEngine::initialize(const EngineCreateInfo& info)
{
    void* hInst = info.platform_instance;
    void* hwnd_ = info.window;

    RECT rect;
    GetClientRect((HWND)hwnd_, &rect);
    width = info.width ? info.width : static_cast<uint32_t>(rect.right - rect.left);
    height = info.height ? info.height : static_cast<uint32_t>(rect.bottom - rect.top);

    this->hInstance = hInst;
    this->hwnd = hwnd_;

    const SoftwareIdentity engine_id = blockviz_engine::identity();
    instance = create_instance(info.application, engine_id);
    create_debug_messenger(instance);
    surface = create_win32_surface(instance, hwnd_, hInst);
    physicalDevice = pick_physical_device(instance, &deviceProps, &deviceMemProps);
    log_engine_startup(deviceProps, engine_id);
    create_device(instance, physicalDevice, &device, &graphicsQueue);

    swapchainImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    swapchainExtent = { width, height };
    s_resized = true;
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_swapchain_targets();
    create_descriptor_set_layout();
    cube_pipe_.create(device, descriptorSetLayout, swapchainImageFormat,
                      swapchain_targets_.depth_format(), width, height);
    {
        PickerResourcesCreateInfo pr{};
        pr.device = device;
        pr.mem_props = &deviceMemProps;
        pr.width = width;
        pr.height = height;
        picker_.create_resources(pr);
        picker_.create_staging(device, &deviceMemProps);
    }
    picker_pipe_.create(device, descriptorSetLayout, picker_.color_format(),
                        swapchain_targets_.depth_format(), width, height);
    create_command_pool();
    create_frame_resources();
    create_descriptor_pool();
    create_descriptor_sets();
    create_sync_objects();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    ImGuiStyle& style = ImGui::GetStyle();

    // Set background color to dark grey
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);

    // Set text color to light grey
    style.Colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    // Set button color
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f); // Dark button color
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f); // Slightly lighter on hover

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplVulkan_InitInfo imgui_vk = {};

    imgui_vk.ApiVersion = VK_API_VERSION_1_3;
    imgui_vk.Instance = instance;
    imgui_vk.PhysicalDevice = physicalDevice;
    imgui_vk.Device = device;
    imgui_vk.QueueFamily = 0;
    imgui_vk.Queue = graphicsQueue;
    imgui_vk.DescriptorPool = descriptorPool;
    imgui_vk.PipelineCache = VK_NULL_HANDLE;
    imgui_vk.RenderPass = VK_NULL_HANDLE;
    imgui_vk.UseDynamicRendering = true;
    imgui_vk.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    imgui_vk.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    imgui_vk.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;
    const VkFormat depth_fmt = swapchain_targets_.depth_format();
    imgui_vk.PipelineRenderingCreateInfo.depthAttachmentFormat = depth_fmt;
    imgui_vk.Subpass = 0;
    imgui_vk.MinImageCount = 2;
    imgui_vk.ImageCount = MAX_FRAMES_IN_FLIGHT;
    imgui_vk.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    imgui_vk.Allocator = NULL;
    imgui_vk.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&imgui_vk);


    meshArena = new MeshArena();
    if (!meshArena->create(device, &deviceMemProps, swapchainImageFormat, depth_fmt))
    {
        printf("Failed to create MeshArena for debug drawing\n");
    }

}   /* Init() */


void VulkanEngine::request_detail_refill_unlocked(const std::string& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(detail_refill_mutex_);
    detail_refill_hash_ = hash;
}

void VulkanEngine::pin_and_maybe_refill(const std::string& hash, bool has_txns)
{
    if (!scene_)
        return;
    // Pin selection so prune keeps full payloads for this id.
    scene_->detail_store().set_full_detail_pin(hash);
    if (!hash.empty() && !has_txns)
        request_detail_refill_unlocked(hash);
}

void VulkanEngine::clear_selection_unlocked()
{
    selected_hash_.clear();
    selected_block = AlphBlock{};
    end_look_aim();
    if (scene_)
        scene_->detail_store().set_full_detail_pin({});
}

void VulkanEngine::clear_selection()
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    clear_selection_unlocked();
}

void VulkanEngine::set_selection_unlocked(const std::string& hash)
{
    if (hash.empty())
    {
        clear_selection_unlocked();
        return;
    }
    if (hash == selected_hash_ && selected_block.hash == hash && !selected_block.txns.empty())
        return;

    selected_hash_ = hash;
    // Prefer detail store only (own mutex) — never lock scene from selection path
    // to avoid ABBA deadlock with render_loop (scene then selection).
    if (scene_)
    {
        if (auto d = scene_->detail_store().get(hash))
            selected_block = std::move(*d);
        else
        {
            selected_block = AlphBlock{};
            selected_block.hash = hash;
        }
        pin_and_maybe_refill(hash, !selected_block.txns.empty());
    }
    else
    {
        selected_block = AlphBlock{};
        selected_block.hash = hash;
    }
}

void VulkanEngine::set_selection(const std::string& hash)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    set_selection_unlocked(hash);
}

std::string VulkanEngine::consume_detail_refill_request()
{
    std::lock_guard<std::mutex> lock(detail_refill_mutex_);
    std::string out = std::move(detail_refill_hash_);
    detail_refill_hash_.clear();
    return out;
}

bool VulkanEngine::is_selected(const std::string& hash) const
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    return !hash.empty() && selected_hash_ == hash;
}

AlphBlock VulkanEngine::copy_selected_block() const
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    return selected_block;
}

void VulkanEngine::refresh_selection_if_needed(BlockScene& scene)
{
    // Caller holds scene.mutex() and selection_mutex_.
    if (selected_hash_.empty())
        return;
    if (selected_block.hash != selected_hash_ || selected_block.txns.empty())
    {
        selected_block = scene.resolve_detail(selected_hash_);
        // If still slim after store catch-up, re-request network rehydrate (PR11).
        if (selected_block.txns.empty())
            pin_and_maybe_refill(selected_hash_, false);
        else
            scene.detail_store().set_full_detail_pin(selected_hash_);
    }
}


void VulkanEngine::start()
{
    running = true;
    renderThread = std::thread(&VulkanEngine::render_loop, this);
}

void VulkanEngine::stop()
{
    running = false;
    if (renderThread.joinable())
        renderThread.join();
}


void VulkanEngine::render_loop()
{
    const double frameTimeMin = 1000.0 / 60; // ~16.67ms for 60Hz
    LARGE_INTEGER freq, t1, t2;
    double t, dt;

    t = dt = 0.0;
    QueryPerformanceFrequency(&freq);

    while (running)
    {
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        //start frame
        QueryPerformanceCounter(&t1);

        debugDrawer.clear();

        std::string selected_hash_local;
        std::string hovered_hash_local;
        AlphBlock selected_detail_local;

        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            selected_hash_local = selected_hash_;
            hovered_hash_local = hovered_hash_;
            selected_detail_local = selected_block;
        }

        UiSnapshot frame_ui{};
        std::vector<std::string> frame_pick_map;

        if (scene_)
        {
            std::unique_lock<std::mutex> lock(scene_->mutex());

            LayoutParams layout_params;
            layout_params.meters_per_height = meters_per_height;
            layout_params.base_radius = 20.0f;
            layout_params.lane_count = 16;

            // Layout from BlockGraph node snapshot
            const std::vector<GraphNode> graph_nodes = scene_->nodes_snapshot();
            LayoutResult layout = polar_layout_.build(graph_nodes, layout_params);
            const auto& block_positions = layout.positions;

            {
                std::lock_guard<std::mutex> slock(selection_mutex_);
                refresh_selection_if_needed(*scene_);
                selected_hash_local = selected_hash_;
                hovered_hash_local = hovered_hash_;
                selected_detail_local = selected_block;
            }

            // Selection world pos → rotate-into aim (frozen dir, held until right-click)
            has_look_target_ = false;
            if (!selected_hash_local.empty())
            {
                auto lit = block_positions.find(selected_hash_local);
                if (lit != block_positions.end())
                {
                    selected_look_pos_ = lit->second;
                    has_look_target_ = true;
                }
            }

            // Build GPU instances + pick map
            std::vector<GpuInstance> gpu_instances;
            gpu_instances.reserve(layout.placements.size());
            frame_pick_map.reserve(layout.placements.size());
            for (const PlacedBlock& placed : layout.placements)
            {
                if (gpu_instances.size() >= static_cast<size_t>(MAX_INSTANCES))
                {
                    printf("Instance buffer full\n");
                    break;
                }

                glm::vec3 color = placed.color;
                if (!selected_hash_local.empty() && placed.hash == selected_hash_local)
                    color = glm::mix(color, glm::vec3(1.f, 1.f, 1.f), 0.45f);
                else if (!hovered_hash_local.empty() && placed.hash == hovered_hash_local)
                    color = glm::mix(color, glm::vec3(1.f, 0.9f, 0.4f), 0.35f);

                gpu_instances.push_back(GpuInstance{ placed.pos, color });
                frame_pick_map.push_back(placed.hash);
            }

            const glm::vec3 eye = camera_eye();
            if (has_look_target_ && selected_hash_local != look_aim_hash_)
                begin_look_aim(eye, selected_look_pos_, selected_hash_local);
            else if (selected_hash_local.empty() && look_engaged_)
                end_look_aim();
            update_look_direction(last_frame_dt_sec_, eye);
            const CameraUBO camera = build_camera_ubo();
            FrameSubmit submit{};
            submit.instances = gpu_instances.empty() ? nullptr : gpu_instances.data();
            submit.instance_count = gpu_instances.size();
            submit.camera = camera;
            submit.client_seq = ++submit_seq_;
            publish_frame(submit, frame_pick_map);

            // Dependency arrows (render-thread debug meshes; domain still locked)
            {
                const float tip_len    = std::max(0.18f, meters_per_height * 0.08f);
                const float tip_rad    = std::max(0.06f, meters_per_height * 0.03f);
                const float shaft_r    = tip_rad * 0.4f;
                const float clearance  = std::max(0.55f, meters_per_height * 0.12f);
                constexpr uint32_t kDepRadial    = 8;
                constexpr uint32_t kMaxDepArrows = 512;
                uint32_t arrow_count = 0;

                auto add_dep_arrow = [&](const glm::vec3& from, const glm::vec3& to,
                                         const glm::vec4& color, float tip_scale = 1.f)
                {
                    if (arrow_count >= kMaxDepArrows)
                        return;
                    glm::vec3 from_inset, to_inset;
                    if (!inset_segment(from, to, clearance, from_inset, to_inset))
                        return;
                    debugDrawer.add_arrow(from_inset, to_inset, color,
                                          tip_len * tip_scale, tip_rad * tip_scale,
                                          shaft_r * tip_scale, kDepRadial);
                    ++arrow_count;
                };

                auto draw_deps_of = [&](const AlphBlock& block, const glm::vec4& color, float tip_scale)
                {
                    auto to_it = block_positions.find(block.hash);
                    if (to_it == block_positions.end())
                        return;
                    for (const std::string& dep_hash : block.deps)
                    {
                        if (arrow_count >= kMaxDepArrows)
                            break;
                        auto from_it = block_positions.find(dep_hash);
                        if (from_it == block_positions.end())
                            continue;
                        add_dep_arrow(from_it->second, to_it->second, color, tip_scale);
                    }
                };

                // A) Active tip BlockFlow edges — tip nodes from graph (max height / lane)
                for (const NodeId& tip_hash : scene_->tip_ids())
                {
                    if (auto d = scene_->detail_store().get(tip_hash))
                        draw_deps_of(*d, kActiveArrowColor, 1.f);
                }

                const glm::vec4 kMissingOutline(0.75f, 0.75f, 0.8f, 0.9f);
                const float ghost_half = 1.0f;

                auto draw_selection_deps = [&](const AlphBlock& block)
                {
                    auto to_it = block_positions.find(block.hash);
                    if (to_it == block_positions.end())
                        return;
                    const glm::vec3& to_pos = to_it->second;
                    const int parent_lane = block.chain_idx();
                    int missing_i = 0;

                    for (const std::string& dep_hash : block.deps)
                    {
                        auto from_it = block_positions.find(dep_hash);
                        if (from_it != block_positions.end())
                        {
                            add_dep_arrow(from_it->second, to_pos, kSelectionArrowColor, 1.15f);
                            continue;
                        }

                        const float angle =
                            ((static_cast<float>(parent_lane) + 0.35f +
                              0.08f * static_cast<float>(missing_i)) /
                             16.0f) *
                            2.0f * 3.14159265f;
                        const float radius = 20.0f * 0.9f;
                        glm::vec3 ghost(
                            radius * std::cos(angle),
                            radius * std::sin(angle),
                            to_pos.z + meters_per_height);
                        debugDrawer.add_wire_box(ghost, ghost_half, kMissingOutline);
                        debugDrawer.add_line(to_pos, ghost, kMissingOutline);
                        ++missing_i;
                    }
                };

                if (!selected_hash_local.empty() && selected_detail_local.hash == selected_hash_local)
                    draw_selection_deps(selected_detail_local);
                else if (!selected_hash_local.empty())
                {
                    if (auto d = scene_->detail_store().get(selected_hash_local))
                        draw_selection_deps(*d);
                }

                if (!hovered_hash_local.empty() && hovered_hash_local != selected_hash_local)
                {
                    if (auto d = scene_->detail_store().get(hovered_hash_local))
                        draw_deps_of(*d, kHoverArrowColor, 1.05f);
                }
            }

            // Build UiSnapshot while still under scene lock (feed/total stable)
            frame_ui.total_blocks = scene_->total_blocks();
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
            for (const RecentFeedItem& b : scene_->feed())
            {
                FeedEntry e;
                e.hash = b.hash;
                e.chainFrom = b.chainFrom;
                e.chainTo = b.chainTo;
                e.height = b.height;
                frame_ui.feed.push_back(std::move(e));
            }
        }
        else
        {
            has_look_target_ = false;
            const glm::vec3 eye = camera_eye();
            update_look_direction(last_frame_dt_sec_, eye);
            FrameSubmit submit{};
            submit.camera = build_camera_ubo();
            submit.client_seq = ++submit_seq_;
            publish_frame(submit, frame_pick_map);
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
        }

        // PR7: publish overlay snapshot + apply latest GPU frame (triple-buffer acquire)
        publish_ui_snapshot(std::move(frame_ui));
        apply_published_frame();

        // PR8: app chrome only via IUiOverlay (explorer URLs live in overlay TU)
        if (overlay_)
            overlay_->draw();

        ImGui::Render();
        // GPU buffers filled via apply_published_frame (PR7); viewProj set there for debug draw
        render();

        elapsedSeconds += static_cast<float>(dt) * 0.001f; // auto scroll along view

        do //end frame
        {
            QueryPerformanceCounter(&t2);
            dt = static_cast<double>((t2.QuadPart - t1.QuadPart) * 1000LL / freq.QuadPart);
            t += dt;
        } while (dt <= frameTimeMin);

        // Next frame's look slerp uses this dt (seconds)
        last_frame_dt_sec_ = static_cast<float>(dt) * 0.001f;
        if (last_frame_dt_sec_ < 1e-4f || last_frame_dt_sec_ > 0.1f)
            last_frame_dt_sec_ = 1.f / 60.f;

    }

}   /* render_loop() */


void VulkanEngine::render()
{
    static uint64_t s_frameCounter;
    static uint32_t imageIndex;
    static bool     resizing = false;

    VkResult 		result;
    VkCommandBuffer commandBuffer;
    VkSemaphore     imageAvailableSemaphore;
    VkSemaphore     renderFinishedSemaphore;

    std::lock_guard<std::mutex> lk(renderMutex);

    currentFrame = s_frameCounter % MAX_FRAMES_IN_FLIGHT;

    FramesInFlight& frame = inFlightFrames[currentFrame];
    commandBuffer = frame.commandBuffer;

    frame_sync_.wait_frame(device, static_cast<uint32_t>(currentFrame));

    if (resizing)
    {
        resize_internal();
        resizing = false;
    }

    if (inFlightFrames[currentFrame].pendingPick)
    {
        const PickKind kind = inFlightFrames[currentFrame].pickKind;
        inFlightFrames[currentFrame].pendingPick = false;
        inFlightFrames[currentFrame].pickKind = PickKind::None;

        const uint32_t picked = picker_.read_object_id(device);
        const auto& pick_map = inFlightFrames[currentFrame].pick_map;

        std::string resolved;
        if (picked != kPickerInvalidId && picked < pick_map.size())
            resolved = pick_map[picked];

        if (kind == PickKind::Click)
        {
            if (!resolved.empty())
                set_selection(resolved);
        }
        else if (kind == PickKind::Hover)
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            hovered_hash_ = resolved;
        }
    }

    inFlightFrames[currentFrame].pick_map = pick_id_to_hash_;
    inFlightFrames[currentFrame].pick_frame_seq = gpu_frame_seq_;

    imageAvailableSemaphore = frame_sync_.image_available(static_cast<uint32_t>(currentFrame));
    result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        resizing = true;
    renderFinishedSemaphore = frame_sync_.render_finished(imageIndex);

    vkResetCommandBuffer(commandBuffer, 0);
    record_command_buffer(commandBuffer, imageIndex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    const uint64_t signalValues[] = { s_frameCounter + 1, 0 };

    VkCommandBufferSubmitInfo cbSubmitInfo{};
    cbSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSubmitInfo.commandBuffer = commandBuffer;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = imageAvailableSemaphore;

    VkSemaphoreSubmitInfo signalInfo[2]{};
    signalInfo[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo[0].semaphore = frame_sync_.timeline();
    signalInfo[0].value = signalValues[0];
    signalInfo[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    signalInfo[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo[1].semaphore = renderFinishedSemaphore;
    signalInfo[1].value = signalValues[1];
    signalInfo[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo2 submitInfo2{};
    submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo2.commandBufferInfoCount = 1;
    submitInfo2.pCommandBufferInfos = &cbSubmitInfo;
    submitInfo2.signalSemaphoreInfoCount = 2;
    submitInfo2.pSignalSemaphoreInfos = signalInfo;
    submitInfo2.waitSemaphoreInfoCount = 1;
    submitInfo2.pWaitSemaphoreInfos = &waitInfo;

    if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo2, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(graphicsQueue, &presentInfo);

    frame_sync_.set_frame_timeline_value(static_cast<uint32_t>(currentFrame), signalValues[0]);
    s_frameCounter++;

}   /* render() */


void VulkanEngine::Resize()
{
    RECT rect;
    int new_width;
    int new_height;

    GetClientRect((HWND)hwnd, &rect);

    new_width = rect.right - rect.left;
    new_height = rect.bottom - rect.top;

    if (0 == new_width && 0 == new_height)
        return;

    if (new_width == width && new_height == height)
        return;

    std::lock_guard<std::mutex> lk(renderMutex);

    width = new_width;
    height = new_height;

    resize_internal();

}   /* Resize() */


void VulkanEngine::resize_internal()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    if (capabilities.currentExtent.width == 0
     || capabilities.currentExtent.height == 0)
    {
        return;
    }

    vkDeviceWaitIdle(device);

    swapchainExtent = { width, height };
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_swapchain_targets();
    {
        PickerResourcesCreateInfo pr{};
        pr.device = device;
        pr.mem_props = &deviceMemProps;
        pr.width = width;
        pr.height = height;
        picker_.recreate_resources(pr);
    }

    s_resized = true;

}   /* resize() */


void VulkanEngine::create_swapchain_targets()
{
    SwapchainTargetsCreateInfo info{};
    info.device = device;
    info.physical_device = physicalDevice;
    info.mem_props = &deviceMemProps;
    info.swapchain_images = swapchainImages.empty() ? nullptr : swapchainImages.data();
    info.swapchain_image_count = static_cast<uint32_t>(swapchainImages.size());
    info.color_format = swapchainImageFormat;
    info.width = width;
    info.height = height;
    // First call: create. After resize: destroy+create via recreate.
    if (swapchain_targets_.color_view_count() == 0 &&
        swapchain_targets_.depth_view() == VK_NULL_HANDLE)
        swapchain_targets_.create(info);
    else
        swapchain_targets_.recreate(info);
}


void VulkanEngine::create_descriptor_set_layout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout");
    }

}   /* create_descriptor_set_layout() */


void VulkanEngine::create_frame_resources()
{
    FrameResourcesCreateInfo info{};
    info.device = device;
    info.mem_props = &deviceMemProps;
    info.cube_vertices = CUBE_VERTICES;
    info.cube_vertex_bytes = sizeof(CUBE_VERTICES);
    info.cube_indices = CUBE_INDICES;
    info.cube_index_bytes = sizeof(CUBE_INDICES);
    info.max_instances = static_cast<uint32_t>(MAX_INSTANCES);
    frame_resources_.create(info);
    instanceCount = 0;
}


void VulkanEngine::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = 
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = _countof(pool_sizes);
    poolInfo.pPoolSizes = pool_sizes;
    poolInfo.maxSets = 2;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool");
    }

}   /* create_descriptor_pool() */


void VulkanEngine::create_descriptor_sets()
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = frame_resources_.uniform_buffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

}   /* create_descriptor_sets() */


void VulkanEngine::create_command_pool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assume graphics family at 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkAllocateCommandBuffers(device, &allocInfo, &inFlightFrames[i].commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers");
        }
    }
}   /* create_command_pool() */


void VulkanEngine::create_sync_objects()
{
    FrameSyncCreateInfo info{};
    info.device = device;
    info.frames_in_flight = MAX_FRAMES_IN_FLIGHT;
    info.swapchain_image_count = MAX_SWAPCHAIN_IMAGES;
    frame_sync_.create(info);
}


glm::vec3 VulkanEngine::camera_eye() const
{
    float eye_z = static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS);
    float meters_per_second = 1.f;
    if (camera_)
    {
        const CameraState::Snapshot s = camera_->snapshot();
        eye_z = s.eye_z;
        meters_per_second = s.meters_per_second;
    }
    const float cam_z = eye_z - meters_per_second * elapsedSeconds;
    return glm::vec3(0.0f, 0.0f, cam_z);
}

void VulkanEngine::begin_look_aim(const glm::vec3& eye, const glm::vec3& target_pos,
                                  const std::string& hash)
{
    if (hash.empty())
        return;

    glm::vec3 dir = target_pos - eye;
    const float len = glm::length(dir);
    if (len < 0.05f)
        dir = kCamForward;
    else
        dir /= len;

    // Full hard-lookAt angle, frozen at click (hold until right-click / clear)
    look_aim_dir_ = dir;
    look_aim_hash_ = hash;
    look_engaged_ = true;
}

void VulkanEngine::end_look_aim()
{
    look_engaged_ = false;
    look_aim_hash_.clear();
    // look_dir_ slerps toward kCamForward in update_look_direction
}

void VulkanEngine::update_look_direction(float dt, const glm::vec3& eye)
{
    (void)eye;

    // Engaged: hold full rotate-into aim. Disengaged: rotate home to original +Z.
    glm::vec3 desired_dir = look_engaged_ ? look_aim_dir_ : kCamForward;
    const float dlen = glm::length(desired_dir);
    if (dlen < 1e-4f)
        desired_dir = kCamForward;
    else
        desired_dir /= dlen;

    if (!look_dir_init_)
    {
        look_dir_ = desired_dir;
        look_dir_init_ = true;
        return;
    }

    glm::vec3 from = look_dir_;
    const float flen = glm::length(from);
    if (flen < 1e-4f)
        from = kCamForward;
    else
        from /= flen;

    float alpha = 1.0f - std::exp(-kLookOmega * std::max(dt, 1e-4f));
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    const float dot = glm::clamp(glm::dot(from, desired_dir), -1.0f, 1.0f);
    if (dot > 0.9995f)
    {
        look_dir_ = desired_dir;
        return;
    }

    const glm::quat q_delta = glm::rotation(from, desired_dir);
    const glm::quat q_step = glm::slerp(glm::identity<glm::quat>(), q_delta, alpha);
    look_dir_ = glm::normalize(q_step * from);
}

CameraUBO VulkanEngine::build_camera_ubo() const
{
    CameraUBO cam{};

    float meters_per_second = 1.f;
    if (camera_)
        meters_per_second = camera_->snapshot().meters_per_second;

    const glm::vec3 eye = camera_eye();
    glm::vec3 dir = look_dir_init_ ? look_dir_ : kCamForward;
    const float dlen = glm::length(dir);
    if (dlen < 1e-4f)
        dir = kCamForward;
    else
        dir /= dlen;

    glm::vec3 center = eye + dir;
    if (glm::length(center - eye) < 0.05f)
        center = eye + kCamForward;

    const float aspect = (height > 0) ? (float)width / (float)height : 1.f;
    cam.view = glm::lookAt(eye, center, kCamUp);
    cam.proj = glm::perspective(FOV, aspect, NEAR_PLANE, FAR_PLANE);
    cam.view_pos = eye;
    cam.light_pos = center;
    cam.meters = meters_per_second;
    return cam;
}

int VulkanEngine::find_free_gpu_slot_unlocked() const
{
    // Triple buffer: always one free among {0,1,2} when reading and pending are distinct.
    for (int i = 0; i < kGpuSlots; ++i)
    {
        if (i != reading_slot_ && i != pending_slot_)
            return i;
    }
    // Degenerate: prefer overwriting pending (latest-wins still holds)
    return (pending_slot_ >= 0) ? pending_slot_ : 0;
}

void VulkanEngine::submit_frame(const FrameSubmit& frame)
{
    publish_frame(frame, {});
}

void VulkanEngine::publish_frame(const FrameSubmit& frame, const std::vector<std::string>& pick_map)
{
    // Deep-copy only — no GPU work. Latest pending wins if GPU has not acquired yet.
    std::lock_guard<std::mutex> lock(submit_mutex_);

    const int write = find_free_gpu_slot_unlocked();
    GpuFrameSlot& slot = gpu_slots_[write];

    slot.instances.resize(frame.instance_count);
    if (frame.instance_count > 0 && frame.instances)
        std::memcpy(slot.instances.data(), frame.instances, frame.instance_count * sizeof(GpuInstance));
    slot.camera = frame.camera;
    slot.client_seq = frame.client_seq;
    slot.pick_map = pick_map;

    pending_slot_ = write;
}

bool VulkanEngine::apply_published_frame()
{
    int slot_idx = -1;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (pending_slot_ < 0)
            return false;
        reading_slot_ = pending_slot_;
        pending_slot_ = -1;
        slot_idx = reading_slot_;
    }

    const GpuFrameSlot& slot = gpu_slots_[slot_idx];
    instanceCount = frame_resources_.upload_instances(
        slot.instances.empty() ? nullptr : slot.instances.data(),
        slot.instances.size());
    frame_resources_.upload_camera(slot.camera, &viewProj);

    pick_id_to_hash_ = slot.pick_map;
    gpu_frame_seq_ = slot.client_seq;
    return true;
}

void VulkanEngine::publish_ui_snapshot(UiSnapshot snap)
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    if (snap.seq == 0)
        snap.seq = ++ui_snap_seq_;
    else
        ui_snap_seq_ = snap.seq;
    ui_snap_ = std::move(snap);
}

UiSnapshot VulkanEngine::copy_ui_snapshot() const
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    return ui_snap_;
}

void VulkanEngine::update_uniform_buffer()
{
    CameraUBO cam = build_camera_ubo();
    frame_resources_.upload_camera(cam, &viewProj);
}


void VulkanEngine::record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(buffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain_targets_.color_view(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { 0.7f, 0.7f, 0.7f, 1.0f } };

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = swapchain_targets_.depth_view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { { 0, 0 }, { width, height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    if (s_resized)
    {
        pipeline_barrier(buffer, swapchainImages[imageIndex],
            VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 }
        );

        pipeline_barrier(buffer, swapchain_targets_.depth_image(),
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0,1,0,1 }
        );
    }

    vkCmdBeginRendering(buffer, &renderInfo);
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipe_.pipeline);
    vkCmdSetPrimitiveTopology(buffer, topology);

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(buffer, 0, 1, &scissor);

    VkBuffer buffers[] = { frame_resources_.vertex_buffer(), frame_resources_.instance_buffer() };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(buffer, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipe_.layout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindIndexBuffer(buffer, frame_resources_.index_buffer(), 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(buffer, 36, static_cast<uint32_t>(instanceCount), 0, 0, 0); // 36 indices per cube

    // Debug meshes share the main pass (depth LOAD) — single batched draw of frame stream
    if (meshArena)
    {
        meshArena->upload(debugDrawer);
        meshArena->draw(buffer, viewProj);
    }

    ImDrawData* data = ImGui::GetDrawData();
    if (data)
    {
        ImGui_ImplVulkan_RenderDrawData(data, buffer);
    }
    vkCmdEndRendering(buffer);


    //transition swapchain image to present
    pipeline_barrier(buffer, swapchainImages[imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 }
    );

    {
        ImGuiIO& io = ImGui::GetIO();

        // Scene rect: exclude right inspector + bottom toolbar (shared chrome layout)
        const float inspector_w = ui_chrome::inspector_width(static_cast<float>(width));
        const float toolbar_h = ui_chrome::kToolbarHeight;
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        const bool over_scene =
            !io.WantCaptureMouse &&
            mx >= 0.f && my >= 0.f &&
            mx < static_cast<float>(width) - inspector_w &&
            my < static_cast<float>(height) - toolbar_h;

        // Right-click scene: clear selection + rotate camera home to original forward
        if (over_scene && io.MouseClicked[ImGuiMouseButton_Right])
            clear_selection();

        PickKind request = PickKind::None;
        if (over_scene && io.MouseClicked[ImGuiMouseButton_Left])
            request = PickKind::Click;
        else if (kHoverPickEnabled && over_scene)
            request = PickKind::Hover; // continuous readback each frame while over scene

        if (request != PickKind::None)
        {
            PickerRecordParams rp{};
            rp.cmd = buffer;
            rp.mouse_x = static_cast<uint32_t>(mx);
            rp.mouse_y = static_cast<uint32_t>(my);
            rp.width = width;
            rp.height = height;
            rp.viewport_extent = swapchainExtent;
            rp.depth_view = swapchain_targets_.depth_view();
            rp.image_layout_undefined = s_resized;
            rp.pipeline = picker_pipe_.pipeline;
            rp.pipeline_layout = picker_pipe_.layout;
            rp.descriptor_set = descriptorSet;
            rp.vertex_buffer = frame_resources_.vertex_buffer();
            rp.instance_buffer = frame_resources_.instance_buffer();
            rp.index_buffer = frame_resources_.index_buffer();
            rp.instance_count = static_cast<uint32_t>(instanceCount);
            picker_.record_pass(rp);
            inFlightFrames[currentFrame].pendingPick = true;
            inFlightFrames[currentFrame].pickKind = request;
        }
        else if (!over_scene)
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            hovered_hash_.clear();
        }

    }

    if (vkEndCommandBuffer(buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer");
    }

}   /* record_command_buffer() */


void VulkanEngine::cleanup()
{
    vkDeviceWaitIdle(device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (meshArena)
    {
        meshArena->destroy();
        delete meshArena;
        meshArena = nullptr;
    }

    frame_resources_.destroy(device);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    frame_sync_.destroy(device);
    vkDestroyCommandPool(device, commandPool, nullptr);

    picker_.destroy(device);
    picker_pipe_.destroy(device);
    cube_pipe_.destroy(device);

    swapchain_targets_.destroy(device);
    destroy_swapchain(device, swapchain);
    destroy_device(device);

    destroy_surface(instance, surface);
    destroy_debug_messenger(instance);

    destroy_instance(instance);
}

IBlockvizEngine* create_blockviz_engine()
{
    return new VulkanEngine();
}

void destroy_blockviz_engine(IBlockvizEngine* engine)
{
    delete engine;
}

IRenderEngine* create_vulkan_engine()
{
    return create_blockviz_engine();
}

void destroy_render_engine(IRenderEngine* engine)
{
    delete engine;
}
