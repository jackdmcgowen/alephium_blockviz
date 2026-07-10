#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>
#include "vulkan_renderer.hpp"
#include "commands.h"

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


const VkFormat PICKING_FORMAT = VK_FORMAT_R32_UINT;
const uint32_t INVALID_ID = ~0u;
const VkExtent2D PICKING_EXT = { 1, 1 }; //smallest pow2 most drivers like
static uint32_t pickMouseX;
static uint32_t pickMouseY;

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

const glm::vec3 SHARD_COLORS[16] = {
    glm::vec3(1.00f, 0.34f, 0.20f),  // #FF5733 Orange
    glm::vec3(0.20f, 1.00f, 0.34f),  // #33FF57 Green
    glm::vec3(0.20f, 0.34f, 1.00f),  // #3357FF Blue
    glm::vec3(1.00f, 0.20f, 1.00f),  // #FF33FF Pink
    glm::vec3(1.00f, 0.76f, 0.00f),  // #FFC300 Yellow
    glm::vec3(0.85f, 0.97f, 0.65f),  // #DAF7A6 Light Green
    glm::vec3(0.78f, 0.00f, 0.22f),  // #C70039 Dark Red
    glm::vec3(0.34f, 0.09f, 0.27f),  // #581845 Dark Purple
    glm::vec3(1.00f, 1.00f, 1.00f),  // #FFFFFF White
    glm::vec3(0.50f, 0.50f, 0.00f),  // #808000 Olive
    glm::vec3(0.00f, 1.00f, 1.00f),  // #00FFFF Aqua
    glm::vec3(1.00f, 0.75f, 0.80f),  // #FFC0CB Pink
    glm::vec3(0.50f, 0.00f, 0.50f),  // #800080 Purple
    glm::vec3(1.00f, 1.00f, 0.00f),  // #FFFF00 Yellow
    glm::vec3(0.50f, 0.50f, 0.50f),  // #808080 Grey
    glm::vec3(0.00f, 1.00f, 0.00f)   // #00FF00 Lime
};

const VulkanRenderer::VertexNormal VulkanRenderer::CUBE_VERTICES[8] = {
    { glm::vec3(-1, -1,  1), glm::normalize(glm::vec3(-1, -1,  1)) }, // 0
    { glm::vec3(1, -1,  1),  glm::normalize(glm::vec3(1, -1,  1)) }, // 1
    { glm::vec3(-1, -1, -1), glm::normalize(glm::vec3(-1, -1, -1)) }, // 2
    { glm::vec3(-1,  1, -1), glm::normalize(glm::vec3(-1,  1, -1)) }, // 3
    { glm::vec3(-1,  1,  1), glm::normalize(glm::vec3(-1,  1,  1)) }, // 4
    { glm::vec3(1,  1, -1),  glm::normalize(glm::vec3(1,  1, -1)) }, // 5
    { glm::vec3(1, -1, -1),  glm::normalize(glm::vec3(1, -1, -1)) }, // 6
    { glm::vec3(1,  1,  1),  glm::normalize(glm::vec3(1,  1,  1)) }  // 7
};

const uint16_t VulkanRenderer::CUBE_INDICES[36] = {
    6, 0, 2, 6, 1, 0,
    4, 0, 1, 0, 4, 3,
    0, 3, 2, 3, 5, 2,
    5, 6, 2, 6, 5, 7,
    6, 7, 1, 1, 7, 4,
    3, 4, 7, 7, 5, 3
};

static bool s_resized;

static float meters_per_height = ALPH_TARGET_BLOCK_SECONDS;
static float meters_per_second = 1;
static float eye_z = -ALPH_LOOKBACK_WINDOW_SECONDS;

// Modal ImGui chrome (does not cover center 3D view)
static const float kInspectorWidth = 340.f;
static const float kToolbarHeight  = 168.f;
static const float kEyeZStep       = 40.f;   // world units per second while held
static const float kEyeZMin        = -2000.f;
static const float kEyeZMax        = 2000.f;

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

VulkanRenderer::VulkanRenderer()
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
    , pipelineLayout(VK_NULL_HANDLE)
    , graphicsPipeline(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , inFlightFrames{}
    , renderFinishedSemaphores(MAX_SWAPCHAIN_IMAGES)
    , currentFrame(0)
    , vertexBuffer(VK_NULL_HANDLE)
    , vertexBufferMemory(VK_NULL_HANDLE)
    , indexBuffer(VK_NULL_HANDLE)
    , indexBufferMemory(VK_NULL_HANDLE)
    , instanceBuffer(VK_NULL_HANDLE)
    , instanceBufferMemory(VK_NULL_HANDLE)
    , mappedInstanceMemory(nullptr)
    , instanceCount(0)
    , uniformBuffer(VK_NULL_HANDLE)
    , uniformBufferMemory(VK_NULL_HANDLE)
    , descriptorPool(VK_NULL_HANDLE)
    , descriptorSet(VK_NULL_HANDLE)
    , running(false)
    , elapsedSeconds(0.0f)
    , width(0)
    , height(0)
{
}   /* VulkanRenderer() */

void VulkanRenderer::set_scene(BlockScene* scene)
{
    scene_ = scene;
}

VulkanRenderer::~VulkanRenderer()
{
    Stop();
    cleanup();

}   /* ~VulkanRenderer() */


void VulkanRenderer::Init(void *hInstance, void *hwnd)
{
    RECT rect;
    GetClientRect((HWND)hwnd, &rect);
    width = static_cast<uint32_t>(rect.right - rect.left);
    height = static_cast<uint32_t>(rect.bottom - rect.top);

    this->hInstance = hInstance;
    this->hwnd = hwnd;
    instance = create_instance();
    create_debug_messenger(instance);
    surface = create_win32_surface(instance, hwnd, hInstance);
    physicalDevice = pick_physical_device(instance, &deviceProps, &deviceMemProps);
    create_device(instance, physicalDevice, &device, &graphicsQueue);

    swapchainImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    swapchainExtent = { width, height };
    s_resized = true;
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_depth_resources();
    create_image_views();
    create_descriptor_set_layout();
    create_graphics_pipeline();
    create_picker_pipeline();
    create_picker_resources();
    create_command_pool();
    create_vertex_buffer();
    create_index_buffer();
    create_instance_buffer();
    create_uniform_buffer();
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
    ImGui_ImplVulkan_InitInfo info = {};

    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;
    info.QueueFamily = 0; // Assume graphics queue at index 0
    info.Queue = graphicsQueue;
    info.DescriptorPool = descriptorPool;
    info.PipelineCache = VK_NULL_HANDLE;
    info.RenderPass = VK_NULL_HANDLE;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainImageFormat;
    info.PipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;
    info.Subpass = 0;
    info.MinImageCount = 2;
    info.ImageCount = MAX_FRAMES_IN_FLIGHT;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.Allocator = NULL;
    info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init( &info );


    meshArena = new MeshArena();
    if (!meshArena->create(device, &deviceMemProps, swapchainImageFormat, depthFormat))
    {
        printf("Failed to create MeshArena for debug drawing\n");
    }

}   /* Init() */


void VulkanRenderer::clear_selection_unlocked()
{
    selected_hash_.clear();
    selected_block = AlphBlock{};
}

void VulkanRenderer::clear_selection()
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    clear_selection_unlocked();
}

void VulkanRenderer::set_selection_unlocked(const std::string& hash)
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
    }
    else
    {
        selected_block = AlphBlock{};
        selected_block.hash = hash;
    }
}

void VulkanRenderer::set_selection(const std::string& hash)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    set_selection_unlocked(hash);
}

bool VulkanRenderer::is_selected(const std::string& hash) const
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    return !hash.empty() && selected_hash_ == hash;
}

void VulkanRenderer::refresh_selection_if_needed(BlockScene& scene)
{
    // Caller holds scene.mutex() and selection_mutex_.
    if (selected_hash_.empty())
        return;
    if (selected_block.hash != selected_hash_ || selected_block.txns.empty())
        selected_block = scene.resolve_detail_under_lock(selected_hash_);
}


void VulkanRenderer::Start()
{
    running = true;
    renderThread = std::thread(&VulkanRenderer::render_loop, this);

}   /* Start() */


void VulkanRenderer::Stop()
{
    running = false;
    if (renderThread.joinable())
        renderThread.join();

}   /* Stop() */


void VulkanRenderer::render_loop()
{
    const double frameTimeMin = 1000.0 / 60; // ~16.67ms for 60Hz
    LARGE_INTEGER freq, t1, t2;
    double t, dt;

    t = dt = 0.0;
    QueryPerformanceFrequency(&freq);

    int64_t start = static_cast<int64_t>(time(NULL) - ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
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

            LayoutResult layout = polar_layout_.build(scene_->chains(), layout_params);
            const auto& block_positions = layout.positions;

            {
                std::lock_guard<std::mutex> slock(selection_mutex_);
                refresh_selection_if_needed(*scene_);
                selected_hash_local = selected_hash_;
                hovered_hash_local = hovered_hash_;
                selected_detail_local = selected_block;
            }

            // Look-at: aim at selected cube, keep eye on Z track
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

            const CameraUBO camera = build_camera_ubo();
            FrameSubmit submit{};
            submit.instances = gpu_instances.empty() ? nullptr : gpu_instances.data();
            submit.instance_count = gpu_instances.size();
            submit.camera = camera;
            submit.client_seq = ++submit_seq_;
            submit_frame(submit, frame_pick_map);

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

                const auto& chains = scene_->chains();

                for (uint8_t chain_from = 0; chain_from < ALPH_NUM_GROUPS; ++chain_from)
                {
                    for (uint8_t chain_to = 0; chain_to < ALPH_NUM_GROUPS; ++chain_to)
                    {
                        const uint8_t shard_id =
                            static_cast<uint8_t>(chain_from * ALPH_NUM_GROUPS + chain_to);
                        if (shard_id >= chains.size())
                            continue;

                        const auto& heightMap = chains[shard_id];
                        if (heightMap.empty())
                            continue;

                        const auto tip_it = std::prev(heightMap.end());
                        for (const auto& hash_and_block : tip_it->second)
                            draw_deps_of(hash_and_block.second, kActiveArrowColor, 1.f);
                    }
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
                    else
                    {
                        for (const auto& heightMap : chains)
                        {
                            for (const auto& he : heightMap)
                            {
                                auto it = he.second.find(hovered_hash_local);
                                if (it != he.second.end())
                                {
                                    draw_deps_of(it->second, kHoverArrowColor, 1.05f);
                                    goto hover_deps_done;
                                }
                            }
                        }
                    hover_deps_done:;
                    }
                }
            }

            // Build UiSnapshot while still under scene lock (feed/total stable)
            frame_ui.total_blocks = scene_->total_blocks();
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
            for (const AlphBlock& b : scene_->feed())
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
            FrameSubmit submit{};
            submit.camera = build_camera_ubo();
            submit.client_seq = ++submit_seq_;
            submit_frame(submit, frame_pick_map);
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
        }

        // PR7: publish overlay snapshot + apply latest GPU frame (triple-buffer acquire)
        publish_ui_snapshot(std::move(frame_ui));
        apply_published_frame();

        // ImGui reads only the published snapshot (no live scene / selection races)
        const UiSnapshot ui = copy_ui_snapshot();

        // Frame dt for keyboard camera
        const float dt_sec = (dt > 0.0) ? static_cast<float>(dt) * 0.001f : (1.f / 60.f);

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard)
        {
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
                eye_z += kEyeZStep * dt_sec;
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
                eye_z -= kEyeZStep * dt_sec;
            eye_z = std::clamp(eye_z, kEyeZMin, kEyeZMax);
        }

        const float ui_w = static_cast<float>(width);
        const float ui_h = static_cast<float>(height);
        const float inspector_w = std::min(kInspectorWidth, ui_w * 0.35f);
        const float toolbar_h = kToolbarHeight;
        const float scene_w = std::max(1.f, ui_w - inspector_w);

        // --- Toolbar: bottom-left ---
        {
            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
            ImGui::SetNextWindowPos(ImVec2(0, ui_h - toolbar_h), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(scene_w, toolbar_h), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 8.f));
            ImGui::Begin("Blockflow", nullptr, flags);

            int64_t now = time(NULL) * 1000;
            const float bps = ui.total_blocks / (0.001f * (now - start) + 1e-3f);

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            ImGui::SliderFloat("Scroll speed", &meters_per_second, 1.0f, 50.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Camera auto-scroll rate along the chain axis");

            ImGui::Text("z: %.1f  (Up/Down)", eye_z);
            ImGui::SameLine(0.f, 24.f);
            ImGui::Text("blocks: %d", ui.total_blocks);
            ImGui::SameLine(0.f, 24.f);
            ImGui::Text("rate: %1.2f/s", bps);

            ImGui::Separator();
            ImGui::BeginChild("feed", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (const FeedEntry& entry : ui.feed)
            {
                ImGui::PushID(entry.hash.c_str());
                const int shardId = entry.chain_idx();
                ImGui::TextColored(
                    ImVec4(SHARD_COLORS[shardId].r, SHARD_COLORS[shardId].g, SHARD_COLORS[shardId].b, 1.0f),
                    "[%d->%d]", entry.chainFrom, entry.chainTo);
                ImGui::SameLine();
                if (ImGui::Button(entry.hash.c_str()))
                {
                    set_selection(entry.hash);
                    ImGui::SetClipboardText(entry.hash.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        // --- Inspector: right rail (detail from UiSnapshot; re-copy if feed click updated selection) ---
        {
            AlphBlock inspector = ui.selected_detail;
            {
                std::lock_guard<std::mutex> slock(selection_mutex_);
                if (!selected_hash_.empty() &&
                    (inspector.hash != selected_hash_ || inspector.txns.empty()))
                    inspector = selected_block;
            }

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_AlwaysVerticalScrollbar;
            ImGui::SetNextWindowPos(ImVec2(ui_w - inspector_w, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(inspector_w, ui_h), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.94f);
            ImGui::Begin("Block", nullptr, flags);

            char url[512];
            if (!inspector.hash.empty())
            {
                memset(url, 0, sizeof(url));
                snprintf(url, sizeof(url), "https://explorer.alephium.org/blocks/%s",
                         inspector.hash.c_str());

                ImGui::Text("hash:");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(inspector.hash.c_str(), url);

                ImGui::Text("height: %d", inspector.height);

                const int shardId = inspector.chain_idx();
                ImGui::Text("chain:");
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4(SHARD_COLORS[shardId].r, SHARD_COLORS[shardId].g, SHARD_COLORS[shardId].b, 1.0f),
                    "[%d->%d]", inspector.chainFrom, inspector.chainTo);

                ImGui::Separator();
                ImGui::Text("Transactions (%d)", static_cast<int>(inspector.txns.size()));

                for (auto& tx : inspector.txns)
                {
                    memset(url, 0, sizeof(url));
                    snprintf(url, sizeof(url), "https://explorer.alephium.org/transactions/%s",
                             tx.txid.c_str());

                    ImGui::Separator();
                    ImGui::Text("txid:");
                    ImGui::SameLine();
                    ImGui::TextLinkOpenURL(tx.txid.c_str(), url);

                    ImGui::Text("version: %d", tx.version);
                    ImGui::Text("networkId: %d", tx.networkId);
                    ImGui::Text("scriptOpt: %s", tx.scriptOpt.c_str());
                    ImGui::Text("gasAmount: %d", tx.gasAmount);
                    ImGui::Text("gasPrice: %s", tx.gasPrice.c_str());
                    ImGui::Text("inputs: %d  outputs: %d",
                                static_cast<int>(tx.inputs.size()),
                                static_cast<int>(tx.outputs.size()));

                    for (auto& out : tx.outputs)
                    {
                        memset(url, 0, sizeof(url));
                        snprintf(url, sizeof(url), "https://explorer.alephium.org/addresses/%s",
                                 out.address.c_str());
                        ImGui::Bullet();
                        ImGui::TextLinkOpenURL(out.address.c_str(), url);
                        ImGui::SameLine();
                        ImGui::Text("(%s)", out.toAmount().c_str());
                    }
                }
            }
            else
            {
                ImGui::TextWrapped(
                    "Select a block from the feed below or click a cube in the scene.");
                ImGui::Spacing();
                ImGui::TextDisabled("Camera: Up/Down arrows scroll Z");
            }
            ImGui::End();
        }

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

    }

}   /* render_loop() */


void VulkanRenderer::render()
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

    /* wait for the previous (in-flight) frame's use */
    if (frame.value > 0) {

    VkSemaphoreWaitInfo
        waitInfo{};

    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &timeline;
    waitInfo.pValues = &frame.value;

    vkWaitSemaphores( device, &waitInfo, UINT64_MAX );
    }

    if (resizing)
    {
        resize();
        resizing = false;
    }

    if (inFlightFrames[currentFrame].pendingPick)
    {
        const PickKind kind = inFlightFrames[currentFrame].pickKind;
        inFlightFrames[currentFrame].pendingPick = false;
        inFlightFrames[currentFrame].pickKind = PickKind::None;

        const uint32_t picked = read_picker_obj_id(device);
        const auto& pick_map = inFlightFrames[currentFrame].pick_map;

        std::string resolved;
        if (picked != INVALID_ID && picked < pick_map.size())
            resolved = pick_map[picked];

        if (kind == PickKind::Click)
        {
            if (!resolved.empty())
                set_selection(resolved);
            // miss: keep sticky selection
        }
        else if (kind == PickKind::Hover)
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            hovered_hash_ = resolved; // empty clears hover highlight
        }
    }

    // Bind this frame's pick map + seq (matches GPU-visible instances after apply_published_frame)
    inFlightFrames[currentFrame].pick_map = pick_id_to_hash_;
    inFlightFrames[currentFrame].pick_frame_seq = gpu_frame_seq_;

    imageAvailableSemaphore = inFlightFrames[currentFrame].imageAvailableSemaphore;
    result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
    resizing = true;
    }
    renderFinishedSemaphore = renderFinishedSemaphores[imageIndex];

    vkResetCommandBuffer(commandBuffer, 0);
    record_command_buffer(commandBuffer, imageIndex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST );

    uint64_t    signalValues[] = { s_frameCounter + 1, 0 };

    VkCommandBufferSubmitInfo cbSubmitInfo{};
    cbSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSubmitInfo.commandBuffer = commandBuffer;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = imageAvailableSemaphore;

    VkSemaphoreSubmitInfo signalInfo[2]{};
    signalInfo[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo[0].semaphore = timeline;
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
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(graphicsQueue, &presentInfo);

    frame.value = signalValues[0];
    s_frameCounter++;

}   /* render() */


void VulkanRenderer::Resize()
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

    resize();

}   /* Resize() */


void VulkanRenderer::resize()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    if (capabilities.currentExtent.width == 0
     || capabilities.currentExtent.height == 0)
    {
        return;
    }

    vkDeviceWaitIdle(device);



    destroy_image_view(device, picker_imageView);
    destroy_image(device, picker_image, picker_memory);

    for (auto imageView : swapchainImageViews)
    {
        destroy_image_view(device, imageView);
    }
    destroy_image_view(device, depthImageView);

    destroy_image(device, depthImage, depthImageMemory);

    swapchainExtent = { width, height };
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_image_views();

    create_depth_resources();

    create_picker_resources();

    s_resized = true;

}   /* resize() */


void VulkanRenderer::create_depth_resources()
{
    depthFormat = find_depth_format();
    create_image(
        device, 
        width, height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory,
        &deviceMemProps
        );
    depthImageView = create_image_view(device, depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

}   /* create_depth_resources() */


void VulkanRenderer::create_image_views()
{
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++)
    {
        swapchainImageViews[i] = create_image_view(device, swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}   /* create_image_views() */


void VulkanRenderer::create_descriptor_set_layout()
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


void VulkanRenderer::create_picker_resources()
{
    // 1. Picking image
    create_image(
        device,
        width, height,
        PICKING_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        picker_image,
        picker_memory,
        &deviceMemProps
        );

    picker_imageView = create_image_view(device, picker_image, PICKING_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

}   /* create_picker_resources() */


void VulkanRenderer::create_picker_pipeline()
{
    // Simplified shader loading (assume SPIR-V shaders: vert.spv, frag.spv)
    std::vector<uint8_t> vertShaderCode;
    std::vector<uint8_t> fragShaderCode;

    load_shader_source("picker_vert.spv", vertShaderCode);
    load_shader_source("picker_frag.spv", fragShaderCode);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    create_shader_module(device, vertShaderModule, vertShaderCode);
    create_shader_module(device, fragShaderModule, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkVertexInputBindingDescription bindingDescriptions[2];
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(VertexNormal);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexNormal, pos.x);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexNormal, normal.x);

    VkVertexInputAttributeDescription instanceAttributes[2];
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(InstanceData, pos);
    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[1].offset = offsetof(InstanceData, color);

    VkVertexInputAttributeDescription attributes[] = { attributeDescriptions[0], attributeDescriptions[1], instanceAttributes[0], instanceAttributes[1] };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 2;
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchainExtent.width;
    viewport.height = (float)swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE; //reuse depth buffer for picker pass
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange range{};
    range.stageFlags =  VK_SHADER_STAGE_FRAGMENT_BIT;
    range.offset = 0;
    range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;//&picker_descSetLayout;
    pipelineLayoutInfo.pPushConstantRanges = &range;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &picker_pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create picker pipeline layout");
    }


    VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = _countof(dynamicStates);
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorFormat = PICKING_FORMAT;
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = picker_pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &picker_pipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create picker graphics pipeline");
    }

    destroy_shader_module(device, fragShaderModule);
    destroy_shader_module(device, vertShaderModule);

    // 2. Staging buffer (host visible)
    create_buffer(
        device, &deviceMemProps,
        PICKING_EXT.width * PICKING_EXT.height * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory
    );


}   /* create_picker_pipeline() */


void VulkanRenderer::create_graphics_pipeline()
{
    // Simplified shader loading (assume SPIR-V shaders: vert.spv, frag.spv)
    std::vector<uint8_t> vertShaderCode;
    std::vector<uint8_t> fragShaderCode;

    load_shader_source("vert.spv", vertShaderCode);
    load_shader_source("frag.spv", fragShaderCode);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    create_shader_module(device, vertShaderModule, vertShaderCode);
    create_shader_module(device, fragShaderModule, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkVertexInputBindingDescription bindingDescriptions[2];
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(VertexNormal);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexNormal, pos.x);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexNormal, normal.x);

    VkVertexInputAttributeDescription instanceAttributes[2];
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(InstanceData, pos);
    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[1].offset = offsetof(InstanceData, color);

    VkVertexInputAttributeDescription attributes[] = { attributeDescriptions[0], attributeDescriptions[1], instanceAttributes[0], instanceAttributes[1] };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 2;
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;




    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = _countof( dynamicStates );
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &swapchainImageFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    destroy_shader_module(device, fragShaderModule);
    destroy_shader_module(device, vertShaderModule);

}   /* create_graphics_pipeline() */


void VulkanRenderer::create_vertex_buffer()
{
    VkDeviceSize bufferSize = sizeof(CUBE_VERTICES);
    create_buffer(device, &deviceMemProps, 
        bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexBuffer, vertexBufferMemory);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, CUBE_VERTICES, bufferSize);
    vkUnmapMemory(device, vertexBufferMemory);


}   /* create_vertex_buffer() */


void VulkanRenderer::create_index_buffer()
{
    VkDeviceSize bufferSize = sizeof(CUBE_INDICES);
    create_buffer(device, &deviceMemProps, 
        bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexBuffer, indexBufferMemory);

    void* data;
    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, CUBE_INDICES, bufferSize);
    vkUnmapMemory(device, indexBufferMemory);

}   /* create_index_buffer() */


void VulkanRenderer::create_instance_buffer()
{
    VkDeviceSize bufferSize = sizeof(InstanceData) * MAX_INSTANCES;
    create_buffer(device, &deviceMemProps, 
        bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceBuffer, instanceBufferMemory);

    vkMapMemory(device, instanceBufferMemory, 0, bufferSize, 0, &mappedInstanceMemory);
    instanceCount = 0;

}   /* create_instance_buffer() */


void VulkanRenderer::create_uniform_buffer()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    create_buffer(device, &deviceMemProps, 
        bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer, uniformBufferMemory);

}   /* create_uniform_buffer() */


void VulkanRenderer::create_descriptor_pool()
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


void VulkanRenderer::create_descriptor_sets()
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
    bufferInfo.buffer = uniformBuffer;
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


void VulkanRenderer::create_command_pool()
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


void VulkanRenderer::create_sync_objects()
{

    VkSemaphoreTypeCreateInfo semaphoreTypeCI{};
    semaphoreTypeCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeCI.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
    semaphoreTypeCI.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &semaphoreTypeCI;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i)
    {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &inFlightFrames[i].imageAvailableSemaphore);
    }

    semaphoreTypeCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

    vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timeline);

}   /* create_sync_objects() */


CameraUBO VulkanRenderer::build_camera_ubo() const
{
    CameraUBO cam{};
    const float meters = meters_per_second * elapsedSeconds;
    const float cam_z = eye_z - meters;
    glm::vec3 eye(0.0f, 0.0f, cam_z);
    glm::vec3 center(0.0f, 0.0f, cam_z + 1.0f);

    if (has_look_target_)
    {
        center = selected_look_pos_;
        if (glm::length(center - eye) < 0.05f)
            center = eye + glm::vec3(0.0f, 0.0f, 1.0f);
    }

    const float aspect = (height > 0) ? (float)width / (float)height : 1.f;
    cam.view = glm::lookAt(eye, center, glm::vec3(0.0f, -1.0f, 0.0f));
    cam.proj = glm::perspective(FOV, aspect, NEAR_PLANE, FAR_PLANE);
    cam.view_pos = eye;
    cam.light_pos = center;
    cam.meters = meters_per_second;
    return cam;
}

int VulkanRenderer::find_free_gpu_slot_unlocked() const
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

void VulkanRenderer::submit_frame(const FrameSubmit& frame, const std::vector<std::string>& pick_map)
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

bool VulkanRenderer::apply_published_frame()
{
    // Render thread: swap pending → reading and upload to GPU buffers.
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

    instanceCount = 0;
    const size_t n = std::min(slot.instances.size(), static_cast<size_t>(MAX_INSTANCES));
    if (mappedInstanceMemory && n > 0)
    {
        static_assert(sizeof(GpuInstance) == sizeof(InstanceData), "GpuInstance/InstanceData layout mismatch");
        std::memcpy(mappedInstanceMemory, slot.instances.data(), n * sizeof(GpuInstance));
        instanceCount = n;
    }

    {
        UniformBufferObject ubo{};
        ubo.view = slot.camera.view;
        ubo.proj = slot.camera.proj;
        ubo.lightPos = slot.camera.light_pos;
        ubo.viewPos = slot.camera.view_pos;
        ubo.meters = slot.camera.meters;
        viewProj = ubo.proj * ubo.view;

        void* data = nullptr;
        vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
        if (data)
        {
            std::memcpy(data, &ubo, sizeof(ubo));
            vkUnmapMemory(device, uniformBufferMemory);
        }
    }

    pick_id_to_hash_ = slot.pick_map;
    gpu_frame_seq_ = slot.client_seq;
    return true;
}

void VulkanRenderer::publish_ui_snapshot(UiSnapshot snap)
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    if (snap.seq == 0)
        snap.seq = ++ui_snap_seq_;
    else
        ui_snap_seq_ = snap.seq;
    ui_snap_ = std::move(snap);
}

UiSnapshot VulkanRenderer::copy_ui_snapshot() const
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    return ui_snap_;
}

void VulkanRenderer::update_uniform_buffer()
{
    // Legacy camera-only refresh (keeps current instance buffer)
    CameraUBO cam = build_camera_ubo();
    UniformBufferObject ubo{};
    ubo.view = cam.view;
    ubo.proj = cam.proj;
    ubo.lightPos = cam.light_pos;
    ubo.viewPos = cam.view_pos;
    ubo.meters = cam.meters;
    viewProj = ubo.proj * ubo.view;
    void* data = nullptr;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    if (data)
    {
        std::memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uniformBufferMemory);
    }
}   /* update_uniform_buffer() */


uint32_t VulkanRenderer::read_picker_obj_id(VkDevice device)
{
    uint32_t* ptr;
    std::vector<uint32_t> id(PICKING_EXT.width * PICKING_EXT.height);

    vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, (void**)&ptr);
    memcpy(id.data(), ptr, PICKING_EXT.width * PICKING_EXT.height * sizeof(uint32_t));
    vkUnmapMemory(device, stagingMemory);

    return (id[0] == INVALID_ID) ? ~0u : id[0];

}   /* read_picker_obj_id() */


void VulkanRenderer::record_picker_pass(VkCommandBuffer buffer, uint32_t mouseX, uint32_t mouseY, uint32_t instanceOffset)
{
    VkImageSubresourceRange         subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 };

    // Transition to COLOR_ATTACHMENT_OPTIMAL (assume coming from UNDEFINED or TRANSFER_SRC)
    if (s_resized)
    {
        pipeline_barrier(buffer, picker_image,
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 });

    }
    else
    {
        pipeline_barrier(buffer, picker_image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 });
    }

    // Begin rendering


    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = picker_imageView,
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color.uint32[0] = INVALID_ID;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthImageView,
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { { 0, 0 }, { width, height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(buffer,  &renderInfo);

    // Scissor = 1x1 at mouse position
    VkRect2D scissor{};

    scissor.offset = { (int32_t)mouseX, (int32_t)mouseY };
    scissor.extent = { 1, 1 };
    vkCmdSetScissor(buffer, 0, 1, &scissor);

    // Viewport can stay full image
    VkViewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = (float)swapchainExtent.width;
    vp.height = (float)swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(buffer, 0, 1, &vp);

    // Bind your picking pipeline (color write only, no depth usually)
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, picker_pipeline);
    vkCmdSetPrimitiveTopology(buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    // Push constants with mouse position
    PushConstants pc{};
    pc.mouseX = mouseX;
    pc.mouseY = mouseY;
    pc.instanceOffset = instanceOffset;

    vkCmdPushConstants(buffer, picker_pipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(PushConstants), &pc);

    VkBuffer buffers[] = { vertexBuffer, instanceBuffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(buffer, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, picker_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindIndexBuffer(buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(buffer, 36, static_cast<uint32_t>(instanceCount), 0, 0, 0); // 36 indices per cube

    //vkCmdEndRenderPass(buffer);
    vkCmdEndRendering(buffer);

    // Transition to TRANSFER_SRC (assume coming from UNDEFINED or TRANSFER_SRC)
    pipeline_barrier(buffer, picker_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1 }
    );

    // Copy 4x4 (or whole small image) to staging
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageOffset = { (int32_t)mouseX, (int32_t)mouseY, 0 };
    copyRegion.imageExtent = { PICKING_EXT.width, PICKING_EXT.height, 1 };

    vkCmdCopyImageToBuffer(buffer,
        picker_image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1, &copyRegion);

}   /* record_picker_pass() */


void VulkanRenderer::record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(buffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchainImageViews[imageIndex],
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { 0.7f, 0.7f, 0.7f, 1.0f } };

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthImageView,
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

        pipeline_barrier(buffer, depthImage,
            VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0,1,0,1 }
        );
    }

    vkCmdBeginRendering(buffer, &renderInfo);
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
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

    VkBuffer buffers[] = { vertexBuffer, instanceBuffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(buffer, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindIndexBuffer(buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
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

        // Scene rect: exclude right inspector + bottom toolbar
        const float inspector_w = std::min(kInspectorWidth, static_cast<float>(width) * 0.35f);
        const float toolbar_h = kToolbarHeight;
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        const bool over_scene =
            !io.WantCaptureMouse &&
            mx >= 0.f && my >= 0.f &&
            mx < static_cast<float>(width) - inspector_w &&
            my < static_cast<float>(height) - toolbar_h;

        PickKind request = PickKind::None;
        if (over_scene && io.MouseClicked[ImGuiMouseButton_Left])
            request = PickKind::Click;
        else if (kHoverPickEnabled && over_scene)
            request = PickKind::Hover; // continuous readback each frame while over scene

        if (request != PickKind::None)
        {
            pickMouseX = static_cast<uint32_t>(mx);
            pickMouseY = static_cast<uint32_t>(my);
            record_picker_pass(buffer, pickMouseX, pickMouseY);
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


VkFormat VulkanRenderer::find_depth_format()
{
    VkFormat candidates[] = 
        {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
        };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported depth format");

}   /* find_depth_format() */


void VulkanRenderer::cleanup()
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

    if (mappedInstanceMemory)
    {
        vkUnmapMemory(device, instanceBufferMemory);
    }
    destroy_image_view(device, depthImageView);
    destroy_image(device, depthImage, depthImageMemory);

    destroy_buffer(device, vertexBuffer, vertexBufferMemory);
    destroy_buffer(device, indexBuffer, indexBufferMemory);
    destroy_buffer(device, instanceBuffer, instanceBufferMemory);
    destroy_buffer(device, uniformBuffer, uniformBufferMemory);

    destroy_buffer(device, stagingBuffer, stagingMemory);


    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    for (int i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
    }
    vkDestroySemaphore(device, timeline, nullptr);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(device, inFlightFrames[i].imageAvailableSemaphore, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);


    destroy_image_view(device, picker_imageView);
    destroy_image(device, picker_image, picker_memory);

    vkDestroyPipeline(device, picker_pipeline, nullptr);
    vkDestroyPipelineLayout(device, picker_pipelineLayout, nullptr);


    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    for (auto imageView : swapchainImageViews)
    {
        destroy_image_view(device, imageView);
        
    }
    destroy_swapchain(device, swapchain);
    destroy_device(device);

    destroy_surface(instance, surface);
    destroy_debug_messenger(instance);

    destroy_instance(instance);

}   /* cleanup() */
