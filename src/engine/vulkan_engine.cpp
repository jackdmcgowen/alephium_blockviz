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

// Camera rotate-into (selection) / rotate-home (right-click clear)
static constexpr float kLookOmega = 10.0f; // slerp ease rate (higher = snappier)
static const glm::vec3 kCamUp(0.0f, -1.0f, 0.0f);
static const glm::vec3 kCamForward(0.0f, 0.0f, 1.0f); // original free view

VulkanEngine::VulkanEngine()
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
    , computeCommandBuffer(VK_NULL_HANDLE)
    , inFlightFrames{}
    , currentFrame(0)
    , instanceCount(0)
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

void VulkanEngine::set_frame_source(IFrameSource* source)
{
    frame_source_ = source;
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
    create_device(instance, physicalDevice, surface, &device, &queues_);

    swapchainImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    swapchainExtent = { width, height };
    s_resized = true;
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_swapchain_targets();
    create_frame_resources();
    create_frame_descriptors();
    cube_pipe_.create(device, frame_descriptors_.layout(), swapchainImageFormat,
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
    picker_pipe_.create(device, frame_descriptors_.layout(), picker_.color_format(),
                        swapchain_targets_.depth_format(), width, height);
    create_command_pool();
    create_sync_objects();

    {
        SobelComputeCreateInfo sci{};
        sci.device = device;
        sci.mem_props = &deviceMemProps;
        sci.width = width;
        sci.height = height;
        sci.depth_format = swapchain_targets_.depth_format();
        sci.frame_ubo_layout = frame_descriptors_.layout();
        sci.graphics_family = queues_.family_index(QueueType::_3D);
        sci.compute_family = queues_.family_index(QueueType::CMP);
        sobel_.create(sci);
    }

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
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

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

        // Catch up selection detail under scene lock (engine-owned policy).
        if (scene_)
        {
            std::unique_lock<std::mutex> lock(scene_->mutex());
            std::lock_guard<std::mutex> slock(selection_mutex_);
            refresh_selection_if_needed(*scene_);
            selected_hash_local = selected_hash_;
            hovered_hash_local = hovered_hash_;
            selected_detail_local = selected_block;
        }

        UiSnapshot frame_ui{};
        std::vector<std::string> frame_pick_map;

        if (frame_source_)
        {
            FrameSourceInput fin{};
            fin.selected_hash = selected_hash_local;
            fin.hovered_hash = hovered_hash_local;
            fin.selected_detail = selected_detail_local;

            FrameSourceOutput fout{};
            // Host ScenePresenter locks scene; must not hold scene mutex here.
            frame_source_->prepare(fin, fout, &debugDrawer);

            has_look_target_ = fout.has_look_target;
            if (fout.has_look_target)
                selected_look_pos_ = fout.look_target_pos;

            const glm::vec3 eye = camera_eye();
            if (has_look_target_ && selected_hash_local != look_aim_hash_)
                begin_look_aim(eye, selected_look_pos_, selected_hash_local);
            else if (selected_hash_local.empty() && look_engaged_)
                end_look_aim();
            update_look_direction(last_frame_dt_sec_, eye);

            FrameSubmit submit{};
            submit.instances = fout.instances.empty() ? nullptr : fout.instances.data();
            submit.instance_count = fout.instances.size();
            submit.camera = build_camera_ubo();
            submit.client_seq = ++submit_seq_;
            publish_frame(submit, fout.pick_map);

            frame_ui = std::move(fout.ui);
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
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

        publish_ui_snapshot(std::move(frame_ui));
        apply_published_frame();

        if (overlay_)
            overlay_->draw();

        ImGui::Render();
        render();

        elapsedSeconds += static_cast<float>(dt) * 0.001f;

        do
        {
            QueryPerformanceCounter(&t2);
            dt = static_cast<double>((t2.QuadPart - t1.QuadPart) * 1000LL / freq.QuadPart);
            t += dt;
        } while (dt <= frameTimeMin);

        last_frame_dt_sec_ = static_cast<float>(dt) * 0.001f;
        if (last_frame_dt_sec_ < 1e-4f || last_frame_dt_sec_ > 0.1f)
            last_frame_dt_sec_ = 1.f / 60.f;
    }

}   /* render_loop() */


void VulkanEngine::render()
{
    std::lock_guard<std::mutex> lk(renderMutex);

    const FramePresenter::BeginResult begin =
        frame_presenter_.begin(device, frame_sync_, MAX_FRAMES_IN_FLIGHT);
    currentFrame = static_cast<int>(begin.frame_index);

    if (begin.run_deferred_resize)
        resize_internal();

    // Pick resolve for previous frame's pending GPU readback (engine policy).
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

    const FramePresenter::AcquireResult acq =
        frame_presenter_.acquire(device, swapchain, frame_sync_, begin.frame_index);

    // Map selected hash → instance index in this frame's pick map (for single-instance depth).
    uint32_t selected_instance = ~0u;
    {
        std::lock_guard<std::mutex> slock(selection_mutex_);
        if (sobel_.ready() && !selected_hash_.empty())
        {
            for (size_t i = 0; i < pick_id_to_hash_.size(); ++i)
            {
                if (pick_id_to_hash_[i] == selected_hash_)
                {
                    selected_instance = static_cast<uint32_t>(i);
                    break;
                }
            }
        }
    }
    const bool want_sobel = selected_instance != ~0u;

    VkCommandBuffer commandBuffer = inFlightFrames[currentFrame].commandBuffer;
    vkResetCommandBuffer(commandBuffer, 0);
    record_command_buffer(commandBuffer, acq.image_index, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                          /*defer_present=*/want_sobel);

    if (want_sobel)
    {
        submit_frame_with_async_sobel(acq.image_index, commandBuffer, acq.image_available,
                                      acq.render_finished, selected_instance);
    }
    else
    {
        frame_presenter_.submit_and_present(
            queues_.get(QueueType::_3D),
            swapchain,
            frame_sync_,
            begin.frame_index,
            acq.image_index,
            commandBuffer,
            acq.image_available,
            acq.render_finished);
    }

}   /* render() */

void VulkanEngine::submit_frame_with_async_sobel(uint32_t image_index,
                                                 VkCommandBuffer graphics_cb,
                                                 VkSemaphore image_available,
                                                 VkSemaphore render_finished,
                                                 uint32_t selected_instance_index)
{
    // After main+pick: depth-only redraw of the selected instance into sel_depth_,
    // then release that depth for async CMP Sobel (scene depth is not used).
    {
        SelectionDepthDrawParams sp{};
        sp.cmd = graphics_cb;
        sp.ubo_set = frame_descriptors_.set();
        sp.vertex_buffer = frame_resources_.vertex_buffer();
        sp.instance_buffer = frame_resources_.instance_buffer();
        sp.index_buffer = frame_resources_.index_buffer();
        sp.first_instance = selected_instance_index;
        sp.width = width;
        sp.height = height;
        sobel_.record_selection_depth(sp);
        sobel_.record_sel_depth_release_for_compute(graphics_cb);
    }
    frame_recorder_.end(graphics_cb);

    // Submit graphics (_3D): wait image-available, signal g→c
    {
        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = graphics_cb;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = image_available;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo sig{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        sig.semaphore = sobel_.graphics_to_compute();
        sig.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &sig;
        if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("async sobel: graphics submit failed");
    }

    // Compute (CMP): wait g→c, Sobel on selection depth only, signal compute_finished
    {
        vkResetCommandBuffer(computeCommandBuffer, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(computeCommandBuffer, &bi);

        sobel_.record_dispatch(computeCommandBuffer, /*strength=*/14.0f, /*threshold=*/0.001f);
        vkEndCommandBuffer(computeCommandBuffer);

        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = computeCommandBuffer;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = sobel_.graphics_to_compute();
        wait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkSemaphoreSubmitInfo sig{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        sig.semaphore = sobel_.compute_finished();
        sig.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &sig;
        if (vkQueueSubmit2(queues_.get(QueueType::CMP), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("async sobel: compute submit failed");
    }

    // Graphics overlay + present: wait compute, composite edges, present
    {
        VkCommandBuffer overlay_cb = inFlightFrames[(currentFrame + 1) % MAX_FRAMES_IN_FLIGHT].commandBuffer;
        // Reuse a free slot's buffer is wrong if still in flight — allocate one-shot from pool
        // Use computeCommandBuffer is busy; allocate temporary from _3D pool each frame is heavy.
        // Safer: use frame's same buffer only after GPU finished — we submitted it already.
        // Secondary buffer from _3D pool:
        static thread_local VkCommandBuffer s_overlay_cb = VK_NULL_HANDLE;
        if (s_overlay_cb == VK_NULL_HANDLE)
        {
            VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool = commandPool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            vkAllocateCommandBuffers(device, &ai, &s_overlay_cb);
        }
        (void)overlay_cb;
        vkResetCommandBuffer(s_overlay_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(s_overlay_cb, &bi);

        // Color still COLOR_ATTACHMENT — load and draw edges
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = swapchain_targets_.color_view(image_index);
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { width, height } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;
        vkCmdBeginRendering(s_overlay_cb, &ri);

        const float highlight[4] = { 1.0f, 0.85f, 0.15f, 1.35f }; // gold selection edges
        sobel_.record_overlay(s_overlay_cb, width, height, highlight);
        vkCmdEndRendering(s_overlay_cb);

        frame_recorder_.transition_color_to_present(s_overlay_cb, swapchainImages[image_index]);
        vkEndCommandBuffer(s_overlay_cb);

        // Submit via presenter-like path: wait compute_finished + timeline bookkeeping
        const uint64_t signal_value = frame_presenter_.frame_counter() + 1;
        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = s_overlay_cb;
        VkSemaphoreSubmitInfo waits[1]{};
        waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waits[0].semaphore = sobel_.compute_finished();
        waits[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        VkSemaphoreSubmitInfo sigs[2]{};
        sigs[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sigs[0].semaphore = frame_sync_.timeline();
        sigs[0].value = signal_value;
        sigs[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        sigs[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sigs[1].semaphore = render_finished;
        sigs[1].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cb;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = waits;
        submit.signalSemaphoreInfoCount = 2;
        submit.pSignalSemaphoreInfos = sigs;
        if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            throw std::runtime_error("async sobel: overlay submit failed");

        VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        vkQueuePresentKHR(queues_.get(QueueType::_3D), &present);

        frame_sync_.set_frame_timeline_value(static_cast<uint32_t>(currentFrame), signal_value);
        frame_presenter_.notify_submitted_and_presented();
    }
}


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
    {
        SobelComputeCreateInfo sci{};
        sci.device = device;
        sci.mem_props = &deviceMemProps;
        sci.width = width;
        sci.height = height;
        sci.depth_format = swapchain_targets_.depth_format();
        sci.frame_ubo_layout = frame_descriptors_.layout();
        sci.graphics_family = queues_.family_index(QueueType::_3D);
        sci.compute_family = queues_.family_index(QueueType::CMP);
        sobel_.recreate(sci);
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


void VulkanEngine::create_frame_descriptors()
{
    FrameDescriptorsCreateInfo info{};
    info.device = device;
    info.ubo_buffer = frame_resources_.uniform_buffer();
    info.ubo_range = sizeof(UniformBufferObject);
    info.combined_image_sampler_count = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE;
    info.max_sets = 2;
    frame_descriptors_.create(info);
}


void VulkanEngine::create_command_pool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queues_.family_index(QueueType::_3D);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create _3D command pool");

    // CMP pool (async compute) — same family is OK when CMP shares _3D
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
            throw std::runtime_error("Failed to allocate _3D command buffers");
    }

    allocInfo.commandPool = computeCommandPool;
    if (vkAllocateCommandBuffers(device, &allocInfo, &computeCommandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate CMP command buffer");
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


void VulkanEngine::record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex,
                                         VkPrimitiveTopology topology, bool defer_present)
{
    FrameRecordParams rp{};
    rp.cmd = buffer;
    rp.image_index = imageIndex;
    rp.topology = topology;
    rp.after_resize = s_resized;
    rp.width = width;
    rp.height = height;
    rp.scissor_extent = swapchainExtent;
    rp.color_image = swapchainImages[imageIndex];
    rp.color_view = swapchain_targets_.color_view(imageIndex);
    rp.depth_image = swapchain_targets_.depth_image();
    rp.depth_view = swapchain_targets_.depth_view();
    rp.cube_pipeline = cube_pipe_.pipeline;
    rp.cube_layout = cube_pipe_.layout;
    rp.descriptor_set = frame_descriptors_.set();
    rp.vertex_buffer = frame_resources_.vertex_buffer();
    rp.instance_buffer = frame_resources_.instance_buffer();
    rp.index_buffer = frame_resources_.index_buffer();
    rp.instance_count = static_cast<uint32_t>(instanceCount);
    rp.mesh_arena = meshArena;
    rp.debug_drawer = &debugDrawer;
    rp.view_proj = &viewProj;
    rp.imgui_draw_data = ImGui::GetDrawData();
    rp.transition_color_to_present = !defer_present;

    frame_recorder_.record_main(rp);

    // Pick policy stays on the engine (after main pass, same command buffer).
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
            PickerRecordParams pick{};
            pick.cmd = buffer;
            pick.mouse_x = static_cast<uint32_t>(mx);
            pick.mouse_y = static_cast<uint32_t>(my);
            pick.width = width;
            pick.height = height;
            pick.viewport_extent = swapchainExtent;
            pick.depth_view = swapchain_targets_.depth_view();
            pick.image_layout_undefined = s_resized;
            pick.pipeline = picker_pipe_.pipeline;
            pick.pipeline_layout = picker_pipe_.layout;
            pick.descriptor_set = frame_descriptors_.set();
            pick.vertex_buffer = frame_resources_.vertex_buffer();
            pick.instance_buffer = frame_resources_.instance_buffer();
            pick.index_buffer = frame_resources_.index_buffer();
            pick.instance_count = static_cast<uint32_t>(instanceCount);
            picker_.record_pass(pick);
            inFlightFrames[currentFrame].pendingPick = true;
            inFlightFrames[currentFrame].pickKind = request;
        }
        else if (!over_scene)
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            hovered_hash_.clear();
        }
    }

    // Async Sobel path adds depth barriers and ends the CB in submit_frame_with_async_sobel.
    if (!defer_present)
        frame_recorder_.end(buffer);

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

    sobel_.destroy(device);

    // Descriptors reference UBO in frame_resources — free pool before buffers.
    frame_descriptors_.destroy(device);
    frame_resources_.destroy(device);

    frame_sync_.destroy(device);
    if (computeCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, computeCommandPool, nullptr);
        computeCommandPool = VK_NULL_HANDLE;
        computeCommandBuffer = VK_NULL_HANDLE;
    }
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

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
