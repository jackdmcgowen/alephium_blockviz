#include "graphics/pch.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <unordered_map>
#include <string>
#include "graphics/graphics_system.hpp"
#include "domain/alph_block.hpp"
#include "engine/engine_identity.hpp"
#include "graphics/frame/frame_graph/frame_task_graph.hpp"
#include "app/ui_chrome.hpp"
#include "network/commands.h"
#include "graphics/engine_requirements.hpp"

#include "graphics/debug/debug_drawer.h"
#include "graphics/mesh_arena.h"
#include "graphics/frame/frame_shared_state.hpp"

#include "gpu_prv_lib.h"


#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "graphics/platform/gpu_platform.hpp"

// ensure configure can call headless setup

// Avoid Vulkan SDK-only vk_enum_string_helper.h (not always in distro headers).
static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;

    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", static_cast<int>(err));
    if (err < 0)
        abort();

}   /* check_vk_result() */

static constexpr bool kHoverPickEnabled = true;

const VertexNormal GraphicsSystem::CUBE_VERTICES[8] = {
    { glm::vec3(-1, -1,  1), glm::normalize(glm::vec3(-1, -1,  1)) }, // 0
    { glm::vec3(1, -1,  1),  glm::normalize(glm::vec3(1, -1,  1)) }, // 1
    { glm::vec3(-1, -1, -1), glm::normalize(glm::vec3(-1, -1, -1)) }, // 2
    { glm::vec3(-1,  1, -1), glm::normalize(glm::vec3(-1,  1, -1)) }, // 3
    { glm::vec3(-1,  1,  1), glm::normalize(glm::vec3(-1,  1,  1)) }, // 4
    { glm::vec3(1,  1, -1),  glm::normalize(glm::vec3(1,  1, -1)) }, // 5
    { glm::vec3(1, -1, -1),  glm::normalize(glm::vec3(1, -1, -1)) }, // 6
    { glm::vec3(1,  1,  1),  glm::normalize(glm::vec3(1,  1,  1)) }  // 7
};

const uint16_t GraphicsSystem::CUBE_INDICES[36] = {
    6, 0, 2, 6, 1, 0,
    4, 0, 1, 0, 4, 3,
    0, 3, 2, 3, 5, 2,
    5, 6, 2, 6, 5, 7,
    6, 7, 1, 1, 7, 4,
    3, 4, 7, 7, 5, 3
};

static bool s_resized;

GraphicsSystem::GraphicsSystem()
    : hInstance(nullptr)
    , hwnd(nullptr)
    , instance(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
    , deviceProps()
    , deviceMemProps()
    , device(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , swapchain(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , computeCommandPool(VK_NULL_HANDLE)
    , inFlightFrames{}
    , currentFrame(0)
    , instanceCount(0)
    , running(false)
    , width(0)
    , height(0)
{
}   /* GraphicsSystem() */

void GraphicsSystem::set_scene(BlockScene* scene)
{
    scene_ = scene;
}

void GraphicsSystem::set_ui_overlay(IUiOverlay* overlay)
{
    overlay_ = overlay;
}

void GraphicsSystem::set_camera(CameraController* camera)
{
    camera_ = camera;
}

void GraphicsSystem::set_frame_source(IFrameSource* source)
{
    frame_source_ = source;
}

void GraphicsSystem::resize(uint32_t w, uint32_t h)
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

void GraphicsSystem::on_resize()
{
    Resize(); // reads client rect then resize_internal
}

void GraphicsSystem::free()
{
    stop();
    if (inited_)
    {
        cleanup();
        inited_ = false;
    }
}

void GraphicsSystem::request_pick(const PickQuery& /*q*/)
{
    // Picks are driven from the render thread mouse path for now.
}

bool GraphicsSystem::consume_pick(PickResult& out)
{
    out = PickResult{};
    return false;
}

GraphicsSystem::~GraphicsSystem()
{
    free();
}

void GraphicsSystem::init_platform(void* hInst, void* hwnd_)
{
    // Prefer host configure() + engine->init_systems().
    EngineCreateInfo info{};
    info.platform_instance = hInst;
    info.window = hwnd_;
    configure(info);
    init();
}

void GraphicsSystem::configure(const EngineCreateInfo& info)
{
    create_info_ = info;
    configured_ = true;
    gpu_platform_configure_headless(info.headless, info.width, info.height);
}

void GraphicsSystem::init()
{
    if (inited_)
        return;
    if (!configured_)
    {
        std::printf("[gfx] GraphicsSystem::init: call configure() first\n");
        return;
    }

    const EngineCreateInfo& info = create_info_;
    void* hInst = info.platform_instance;
    void* hwnd_ = info.window;
    headless_ = info.headless || gpu_platform_is_headless();
    gpu_platform_configure_headless(headless_, info.width, info.height);

    uint32_t client_w = 0, client_h = 0;
    gpu_platform_get_window_size(hwnd_, &client_w, &client_h);
    width = info.width ? info.width : client_w;
    height = info.height ? info.height : client_h;
    if (width == 0)
        width = headless_ ? 1280u : WDW_WIDTH;
    if (height == 0)
        height = headless_ ? 720u : WDW_HEIGHT;

    this->hInstance = hInst;
    this->hwnd = hwnd_;

    const SoftwareIdentity engine_id = blockviz_engine::identity();
    instance = create_instance(info.application, engine_id, info.enable_validation);
    if (info.enable_validation)
        create_debug_messenger(instance);
    surface = create_platform_surface(instance, hwnd_, hInst);
    physicalDevice = pick_physical_device(instance, &deviceProps, &deviceMemProps);
    log_engine_startup(deviceProps, engine_id);
    query_optional_device_features(physicalDevice, device_optional_features_);
    log_optional_device_features(device_optional_features_);
    // PR1: classic path only. Mesh/cull enable comes in later PRs when usable.
    create_device(instance, physicalDevice, surface, &device, &queues_);
    buffer_manager_.reset(device, &deviceMemProps);
    sampler_table_.create(device);

    swapchainImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    swapchainExtent = { width, height };
    s_resized = true;
    create_swapchain(device, physicalDevice, surface, &swapchain, swapchainImages,
                     swapchainImageFormat, &swapchainExtent);
    width = swapchainExtent.width;
    height = swapchainExtent.height;
    create_swapchain_targets();
    create_frame_resources();
    create_frame_descriptors();
    {
        frame_graph::PassCreateInfo pci{};
        pci.device = device;
        pci.mem_props = &deviceMemProps;
        pci.buffers = &buffer_manager_;
        pci.samplers = &sampler_table_;
        pci.color_format = swapchainImageFormat;
        pci.depth_format = swapchain_targets_.depth_format();
        pci.width = width;
        pci.height = height;
        pci.samples = swapchain_targets_.sample_count();
        pci.alpha_to_coverage = swapchain_targets_.alpha_to_coverage();
        pci.frame_ubo_layout = frame_descriptors_.layout();
        pci.graphics_family = queues_.family_index(QueueType::_3D);
        pci.compute_family = queues_.family_index(QueueType::CMP);
        main_scene_pass_.create(pci);
        // Picker is 1×; selection pick uses cleared depth (independent of MSAA scene depth).
        picker_pass_.create(pci);
        sobel_resources_.create(pci);
        outline_pass_.attach(&sobel_resources_);
        sobel_compute_pass_.attach(&sobel_resources_);
        edge_overlay_pass_.attach(&sobel_resources_);
        outline_pass_.create(pci);
        sobel_compute_pass_.create(pci);
        edge_overlay_pass_.create(pci);
    }
    create_command_pool();
    create_sync_objects();
    sobel_async_.create(device);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    ImGuiStyle& style = ImGui::GetStyle();
    // Alephium-aligned chrome: black panels, white type, brand orange accents.
    // Tokens: docs/brand/alephium_palette.md
    style.WindowRounding = 4.f;
    style.FrameRounding = 3.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.102f, 0.106f, 0.118f, 0.96f); // panel #1A1B1E
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.94f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.165f, 0.173f, 0.192f, 1.f); // #2A2C31
    style.Colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.96f, 1.f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.62f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.20f, 1.f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.f, 0.36f, 0.0f, 0.35f); // brand orange
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.f, 0.36f, 0.0f, 0.55f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.18f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.f, 0.36f, 0.0f, 0.45f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.f, 0.36f, 0.0f, 0.70f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.18f, 0.16f, 1.f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.20f, 0.14f, 1.f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1.f, 0.36f, 0.0f, 1.f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.f, 0.36f, 0.0f, 0.9f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.45f, 0.1f, 1.f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.10f, 0.09f, 1.f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.24f, 1.f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.8f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.30f, 1.f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.f, 0.36f, 0.0f, 0.85f);

    gpu_platform_imgui_init(hwnd);
    ImGui_ImplVulkan_InitInfo imgui_vk = {};

    imgui_vk.ApiVersion = VK_API_VERSION_1_3;
    imgui_vk.Instance = instance;
    imgui_vk.PhysicalDevice = physicalDevice;
    imgui_vk.Device = device;
    imgui_vk.QueueFamily = queues_.family_index(QueueType::_3D);
    imgui_vk.Queue = queues_.get(QueueType::_3D);
    imgui_vk.DescriptorPool = frame_descriptors_.pool();
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


    g_meshArena = new MeshArena();
    if (!g_meshArena->create(device, &deviceMemProps, &buffer_manager_, swapchainImageFormat, depth_fmt,
                           swapchain_targets_.sample_count()))
    {
        printf("Failed to create g_meshArena for debug drawing\n");
    }

    // Frame pass DAG topology: IPass nodes + dependency edges (barriers on edges).
    // Multi-queue submit remains in SobelAsyncPass; graph documents order + access.
    {
        using namespace frame_graph;
        FrameTaskGraph g;
        const uint32_t main_p = g.register_pass(main_scene_pass_);
        const uint32_t pick_p = g.register_pass(picker_pass_);
        const uint32_t sel_p  = g.register_pass(outline_pass_);
        const uint32_t sob_p  = g.register_pass(sobel_compute_pass_);
        const uint32_t ovl_p  = g.register_pass(edge_overlay_pass_);
        const uint32_t pre_p  = g.add_pass(PassNode{ "Present", QueueType::_3D, {} });
        ImageBarrierEdge e{};
        e.resource = ResourceId::SwapchainColor;
        e.from_access = ResourceAccess::ColorAttachment;
        e.to_access = ResourceAccess::ColorAttachment;
        g.add_edge(main_p, pick_p, e);
        e.resource = ResourceId::SelectionDepth;
        e.from_access = ResourceAccess::None;
        e.to_access = ResourceAccess::DepthAttachment;
        g.add_edge(main_p, sel_p, e);
        e.resource = ResourceId::SelectionDepth;
        e.from_access = ResourceAccess::DepthAttachment;
        e.to_access = ResourceAccess::DepthSampled;
        g.add_edge(sel_p, sob_p, e);
        e.resource = ResourceId::SobelEdges;
        e.from_access = ResourceAccess::ComputeStorageWrite;
        e.to_access = ResourceAccess::ShaderRead;
        g.add_edge(sob_p, ovl_p, e);
        e.resource = ResourceId::SwapchainColor;
        e.from_access = ResourceAccess::ColorAttachment;
        e.to_access = ResourceAccess::Present;
        g.add_edge(ovl_p, pre_p, e);
        e.from_access = ResourceAccess::ColorAttachment;
        e.to_access = ResourceAccess::Present;
        g.add_edge(main_p, pre_p, e);
        g.compile(&queues_);
        std::printf("[engine] frame DAG: %s\n", g.debug_order_string().c_str());
    }

    // Frame profiler: TIMESTAMP pools per in-flight slot (off until enabled).
    if (!frame_profiler_.create(device, deviceProps.limits.timestampPeriod,
                                MAX_FRAMES_IN_FLIGHT))
    {
        std::printf("[gfx] FrameProfiler: create failed (CPU scopes only if re-created)\n");
    }

    inited_ = true;
}   /* GraphicsSystem::init() */




void GraphicsSystem::start()
{
    // Idempotent: main calls engine->start() again after adding NetworkSystem.
    // Assigning a new std::thread while renderThread is still joinable throws.
    if (running || renderThread.joinable())
        return;
    if (!inited_)
    {
        std::printf("[gfx] GraphicsSystem::start: call init() first\n");
        return;
    }
    running = true;
    renderThread = std::thread(&GraphicsSystem::render_loop, this);
}

void GraphicsSystem::stop()
{
    running = false;
    if (renderThread.joinable())
        renderThread.join();
    // Drain in-flight Sobel before free() tears down CBs/semaphores.
    sobel_async_.wait_idle(device);
}



void GraphicsSystem::Resize()
{
    uint32_t new_width = 0, new_height = 0;
    gpu_platform_get_window_size(hwnd, &new_width, &new_height);

    if (0 == new_width && 0 == new_height)
        return;

    if (new_width == width && new_height == height)
        return;

    std::lock_guard<std::mutex> lk(renderMutex);

    width = new_width;
    height = new_height;

    resize_internal();

}   /* Resize() */


void GraphicsSystem::resize_internal()
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
    create_swapchain(device, physicalDevice, surface, &swapchain, swapchainImages,
                     swapchainImageFormat, &swapchainExtent);
    width = swapchainExtent.width;
    height = swapchainExtent.height;
    create_swapchain_targets();
    // Recreate present semaphores if image count changed.
    {
        FrameSyncCreateInfo info{};
        info.device = device;
        info.frames_in_flight = MAX_FRAMES_IN_FLIGHT;
        info.swapchain_image_count = static_cast<uint32_t>(swapchainImages.size());
        frame_sync_.create(info);
    }
    {
        sobel_async_.wait_idle(device);
        frame_graph::PassCreateInfo pci{};
        pci.device = device;
        pci.mem_props = &deviceMemProps;
        pci.buffers = &buffer_manager_;
        pci.samplers = &sampler_table_;
        pci.color_format = swapchainImageFormat;
        pci.depth_format = swapchain_targets_.depth_format();
        pci.width = width;
        pci.height = height;
        pci.samples = swapchain_targets_.sample_count();
        pci.alpha_to_coverage = swapchain_targets_.alpha_to_coverage();
        pci.frame_ubo_layout = frame_descriptors_.layout();
        pci.graphics_family = queues_.family_index(QueueType::_3D);
        pci.compute_family = queues_.family_index(QueueType::CMP);
        main_scene_pass_.recreate(pci);
        picker_pass_.recreate(pci);
        // Resources first (images + descriptors), then PSOs that bind their layouts.
        outline_pass_.destroy(device);
        sobel_compute_pass_.destroy(device);
        edge_overlay_pass_.destroy(device);
        sobel_resources_.recreate(pci);
        outline_pass_.attach(&sobel_resources_);
        sobel_compute_pass_.attach(&sobel_resources_);
        edge_overlay_pass_.attach(&sobel_resources_);
        outline_pass_.create(pci);
        sobel_compute_pass_.create(pci);
        edge_overlay_pass_.create(pci);
    }

    s_resized = true;

}   /* resize() */


void GraphicsSystem::create_swapchain_targets()
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


void GraphicsSystem::create_frame_resources()
{
    FrameResourcesCreateInfo info{};
    info.device = device;
    info.mem_props = &deviceMemProps;
    info.buffer_manager = &buffer_manager_;
    info.cube_vertices = CUBE_VERTICES;
    info.cube_vertex_bytes = sizeof(CUBE_VERTICES);
    info.cube_indices = CUBE_INDICES;
    info.cube_index_bytes = sizeof(CUBE_INDICES);
    info.max_instances = static_cast<uint32_t>(MAX_INSTANCES);
    frame_resources_.create(info);
    instanceCount = 0;
}


void GraphicsSystem::create_frame_descriptors()
{
    FrameDescriptorsCreateInfo info{};
    info.device = device;
    info.ubo_buffer = frame_resources_.uniform_buffer();
    info.ubo_range = sizeof(UniformBufferObject);
    info.combined_image_sampler_count = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    info.max_sets = 2;
    frame_descriptors_.create(info);
}


void GraphicsSystem::create_command_pool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queues_.family_index(QueueType::_3D);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create _3D command pool");

    // CMP pool (async compute) â€” same family is OK when CMP shares _3D
    poolInfo.queueFamilyIndex = queues_.family_index(QueueType::CMP);
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &computeCommandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create CMP command pool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkAllocateCommandBuffers(device, &allocInfo, &inFlightFrames[i].commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate _3D main command buffers");
        if (vkAllocateCommandBuffers(device, &allocInfo, &inFlightFrames[i].overlayCommandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate _3D overlay command buffers");
    }

    allocInfo.commandPool = computeCommandPool;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkAllocateCommandBuffers(device, &allocInfo, &inFlightFrames[i].computeCommandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate CMP command buffers");
    }
}   /* create_command_pool() */


void GraphicsSystem::create_sync_objects()
{
    FrameSyncCreateInfo info{};
    info.device = device;
    info.frames_in_flight = MAX_FRAMES_IN_FLIGHT;
    // render_finished is per swapchain image â€” match actual image count.
    info.swapchain_image_count = swapchainImages.empty()
                                     ? static_cast<uint32_t>(MAX_SWAPCHAIN_IMAGES)
                                     : static_cast<uint32_t>(swapchainImages.size());
    frame_sync_.create(info);
    sobel_async_.create(device);
}



void GraphicsSystem::publish_ui_snapshot(UiSnapshot snap)
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    if (snap.seq == 0)
        snap.seq = ++ui_snap_seq_;
    else
        ui_snap_seq_ = snap.seq;
    ui_snap_ = std::move(snap);
}

UiSnapshot GraphicsSystem::copy_ui_snapshot() const
{
    std::lock_guard<std::mutex> lock(ui_snap_mutex_);
    return ui_snap_;
}

void GraphicsSystem::record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex,
                                         VkPrimitiveTopology topology, bool defer_present,
                                         bool capture_screenshot)
{
    FrameProfiler* prof = frame_profiler_.enabled() ? &frame_profiler_ : nullptr;
    main_scene_pass_.begin_command_buffer(buffer, prof);

    frame_graph::PassRecordParams rp{};
    rp.base.cmd = buffer;
    rp.base.frame_index = static_cast<uint32_t>(currentFrame);
    rp.base.image_index = imageIndex;
    rp.topology = topology;
    rp.after_resize = s_resized;
    rp.width = width;
    rp.height = height;
    rp.scissor_extent = swapchainExtent;
    const bool msaa = swapchain_targets_.msaa_enabled();
    rp.color_image = msaa ? swapchain_targets_.msaa_color_image() : swapchainImages[imageIndex];
    rp.color_view = msaa ? swapchain_targets_.msaa_color_view()
                         : swapchain_targets_.color_view(imageIndex);
    rp.depth_image = swapchain_targets_.depth_image();
    rp.depth_view = swapchain_targets_.depth_view();
    rp.samples = swapchain_targets_.sample_count();
    rp.resolve_color_view = msaa ? swapchain_targets_.color_view(imageIndex) : VK_NULL_HANDLE;
    rp.resolve_color_image = msaa ? swapchainImages[imageIndex] : VK_NULL_HANDLE;
    rp.frame_ubo_set = frame_descriptors_.set();
    rp.vertex_buffer = frame_resources_.vertex_buffer();
    rp.instance_buffer = frame_resources_.instance_buffer();
    rp.index_buffer = frame_resources_.index_buffer();
    rp.instance_count = static_cast<uint32_t>(instanceCount);
    rp.mesh_arena = g_meshArena;
    rp.debug_drawer = &g_debugDrawer;
    rp.view_proj = &g_viewProj;
    rp.imgui_draw_data = ImGui::GetDrawData();
    // Screenshot needs COLOR_ATTACHMENT after draws so we can TRANSFER before PRESENT.
    rp.transition_color_to_present = !defer_present && !capture_screenshot;
    rp.profiler = prof;

    main_scene_pass_.record(rp);

    // Pick policy stays on the engine (after main pass, same command buffer).
    {
        ImGuiIO& io = ImGui::GetIO();

        // Scene rect: exclude left Network + right Block rails (full height).
        const float rail_w = ui_chrome::rail_width(static_cast<float>(width));
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        const bool over_scene =
            !io.WantCaptureMouse &&
            mx >= rail_w && my >= 0.f &&
            mx < static_cast<float>(width) - rail_w &&
            my < static_cast<float>(height);

        // Short RMB deselect is in BlockflowOverlay; key 3 / Live reattaches tip.
        // Short LMB click = pick; LMB drag is free look (overlay).
        constexpr float kPickMaxDragSqr = 4.f * 4.f;
        PickKind request = PickKind::None;
        if (over_scene && io.MouseReleased[ImGuiMouseButton_Left] &&
            io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < kPickMaxDragSqr)
            request = PickKind::Click;
        else if (kHoverPickEnabled && over_scene &&
                 !io.MouseDown[ImGuiMouseButton_Left] && !io.MouseDown[ImGuiMouseButton_Right])
            request = PickKind::Hover; // skip hover pick while dragging camera

        if (request != PickKind::None)
        {
            frame_graph::PassRecordParams pick = rp;
            pick.mouse_x = static_cast<uint32_t>(mx);
            pick.mouse_y = static_cast<uint32_t>(my);
            pick.picker_image_undefined = s_resized;
            pick.profiler = frame_profiler_.enabled() ? &frame_profiler_ : nullptr;
            picker_pass_.record(pick);
            inFlightFrames[currentFrame].pendingPick = true;
            inFlightFrames[currentFrame].pickKind = request;
        }
        else if (!over_scene)
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            hovered_hash_.clear();
        }
    }

    // Async Sobel path adds depth barriers and ends the CB in SobelAsyncPass::submit.
    // F12: always copy the presentable swapchain image (resolve target when MSAA), never the
    // multi-sample color attachment — post scene+ImGui, pre-present.
    if (!defer_present)
    {
        if (capture_screenshot)
            record_screenshot_before_present_(buffer, swapchainImages[imageIndex]);
        main_scene_pass_.end_command_buffer(buffer);
    }

}   /* record_command_buffer() */


void GraphicsSystem::enable_frame_profiler(bool enabled)
{
    frame_profiler_.set_enabled(enabled);
    if (enabled)
        profiler_hud_ = true;
}

bool GraphicsSystem::frame_profiler_enabled() const
{
    return frame_profiler_.enabled();
}

void GraphicsSystem::copy_frame_timing_snapshot(FrameTimingSnapshot& out) const
{
    frame_profiler_.copy_snapshot(out);
}

void GraphicsSystem::cleanup()
{
    vkDeviceWaitIdle(device);

    ImGui_ImplVulkan_Shutdown();
    gpu_platform_imgui_shutdown();
    ImGui::DestroyContext();

    frame_profiler_.destroy(device);

    if (g_meshArena)
    {
        g_meshArena->destroy();
        delete g_meshArena;
        g_meshArena = nullptr;
    }

    sobel_async_.destroy(device);
    outline_pass_.destroy(device);
    sobel_compute_pass_.destroy(device);
    edge_overlay_pass_.destroy(device);
    sobel_resources_.destroy(device);

    // Descriptors reference UBO in frame_resources — free pool before buffers.
    frame_descriptors_.destroy(device);
    frame_resources_.destroy(device);

    frame_sync_.destroy(device);
    if (computeCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, computeCommandPool, nullptr);
        computeCommandPool = VK_NULL_HANDLE;
    }
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        inFlightFrames[i].commandBuffer = VK_NULL_HANDLE;
        inFlightFrames[i].computeCommandBuffer = VK_NULL_HANDLE;
        inFlightFrames[i].overlayCommandBuffer = VK_NULL_HANDLE;
    }

    picker_pass_.destroy(device);
    main_scene_pass_.destroy(device);
    destroy_screenshot_staging_();
    sampler_table_.destroy(device);

    swapchain_targets_.destroy(device);
    destroy_swapchain(device, swapchain);
    destroy_device(device);

    destroy_surface(instance, surface);
    destroy_debug_messenger(instance);

    destroy_instance(instance);
}

IGraphicsSystem* create_graphics_system()
{
    return new GraphicsSystem();
}

void destroy_graphics_system(IGraphicsSystem* graphics)
{
    delete graphics;
}

