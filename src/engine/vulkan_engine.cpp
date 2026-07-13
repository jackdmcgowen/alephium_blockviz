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
#include "engine/frame_graph/frame_task_graph.hpp"
#include "app/ui_chrome.hpp"
#include "commands.h"
#include "graphics/engine_requirements.hpp"

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
    , inFlightFrames{}
    , currentFrame(0)
    , instanceCount(0)
    , running(false)
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

void VulkanEngine::set_camera(CameraController* camera)
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
    buffer_manager_.reset(device, &deviceMemProps);

    swapchainImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

    swapchainExtent = { width, height };
    s_resized = true;
    create_swapchain(device, surface, &swapchain, swapchainImages, swapchainImageFormat, swapchainExtent);
    create_swapchain_targets();
    create_frame_resources();
    create_frame_descriptors();
    cube_pipe_.create(device, frame_descriptors_.layout(), swapchainImageFormat,
                      swapchain_targets_.depth_format(), width, height,
                      swapchain_targets_.sample_count(),
                      swapchain_targets_.alpha_to_coverage());
    {
        PickerResourcesCreateInfo pr{};
        pr.device = device;
        pr.mem_props = &deviceMemProps;
        pr.width = width;
        pr.height = height;
        pr.depth_format = swapchain_targets_.depth_format();
        picker_.create_resources(pr);
        picker_.create_staging(&buffer_manager_);
    }
    // Picker is 1×; selection pick uses cleared depth (independent of MSAA scene depth).
    picker_pipe_.create(device, frame_descriptors_.layout(), picker_.color_format(),
                        swapchain_targets_.depth_format(), width, height,
                        VK_SAMPLE_COUNT_1_BIT);
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
    if (!meshArena->create(device, &deviceMemProps, &buffer_manager_, swapchainImageFormat, depth_fmt,
                           swapchain_targets_.sample_count()))
    {
        printf("Failed to create MeshArena for debug drawing\n");
    }

    // G2: document frame pass DAG topology (barriers live on edges).
    {
        using namespace frame_graph;
        FrameTaskGraph g;
        const uint32_t main_p = g.add_pass(PassNode{ "MainColorDepth", QueueType::_3D, {} });
        const uint32_t pick_p = g.add_pass(PassNode{ "Picker", QueueType::_3D, {} });
        const uint32_t sel_p  = g.add_pass(PassNode{ "SelectionDepth", QueueType::_3D, {} });
        const uint32_t sob_p  = g.add_pass(PassNode{ "SobelCompute", QueueType::CMP, {} });
        const uint32_t ovl_p  = g.add_pass(PassNode{ "EdgeOverlay", QueueType::_3D, {} });
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
    if (camera_)
        camera_->clear_look_target();
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

        // Camera: aspect → look-aim → tick → frustum (before geometry submit).
        if (camera_)
        {
            const float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.f;
            camera_->set_aspect(aspect);
        }

        if (frame_source_)
        {
            FrameSourceInput fin{};
            fin.selected_hash = selected_hash_local;
            fin.hovered_hash = hovered_hash_local;
            fin.selected_detail = selected_detail_local;
            // Half-extents for unit cube mesh (±1); slight inflate avoids edge pop.
            fin.instance_half_extents = glm::vec3(1.05f);

            // Look target from layout is still needed for aim; run a thin prepare after
            // tick if we need positions — look aim uses fout after prepare below.
            // Tick camera first without new aim so frustum matches previous look;
            // then set aim when selection changes (target from prepare).

            FrameSourceOutput fout{};
            // Host ScenePresenter locks scene; must not hold scene mutex here.
            // First prepare pass needs frustum — tick camera, then cull.
            Frustum frame_frustum{};
            if (camera_)
            {
                camera_->tick(last_frame_dt_sec_);
                frame_frustum = camera_->frustum();
                fin.frustum = &frame_frustum;
                if (scene_)
                    scene_->set_camera_scroll_z(camera_->scroll_z());
            }

            frame_source_->prepare(fin, fout, &debugDrawer);

            if (camera_)
            {
                if (fout.has_look_target && selected_hash_local != camera_->look_aim_hash())
                    camera_->set_look_target(fout.look_target_pos, selected_hash_local);
                else if (selected_hash_local.empty() && camera_->look_engaged())
                    camera_->clear_look_target();
                // Re-tick look slerp after aim change for this frame's UBO.
                camera_->tick(0.f);
            }

            FrameSubmit submit{};
            submit.instances = fout.instances.empty() ? nullptr : fout.instances.data();
            submit.instance_count = fout.instances.size();
            submit.camera = camera_ ? camera_->ubo() : CameraUBO{};
            submit.client_seq = ++submit_seq_;
            publish_frame(submit, fout.pick_map, fout.confirmed_tip_hashes,
                          fout.incomplete_trace_hashes);

            frame_ui = std::move(fout.ui);
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
        }
        else
        {
            if (camera_)
                camera_->tick(last_frame_dt_sec_);
            FrameSubmit submit{};
            submit.camera = camera_ ? camera_->ubo() : CameraUBO{};
            submit.client_seq = ++submit_seq_;
            // Empty frame-source: clear tips for this frame (paired with empty pick_map).
            publish_frame(submit, frame_pick_map, {}, {});
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
    if (!acq.ok)
        return; // OUT_OF_DATE: resize next begin; do not submit/present

    // Sobel mode matrix (K5): selection gold wins; else confirmed-tip green if kill-switch on.
    SobelFrameRequest sobel_req{};
    bool want_sobel = false;
    if (sobel_.ready())
    {
        uint32_t selected_instance = ~0u;
        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            if (!selected_hash_.empty())
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

        auto resolve_hashes = [&](const std::vector<std::string>& hashes,
                                  size_t cap) {
            std::vector<uint32_t> idxs;
            idxs.reserve(std::min(hashes.size(), cap));
            for (const std::string& h : hashes)
            {
                for (size_t i = 0; i < pick_id_to_hash_.size(); ++i)
                {
                    if (pick_id_to_hash_[i] == h)
                    {
                        idxs.push_back(static_cast<uint32_t>(i));
                        break;
                    }
                }
                if (idxs.size() >= cap)
                    break;
            }
            return idxs;
        };

        // Selection gold > incomplete orange > frontier-tip green (single mode).
        if (selected_instance != ~0u)
        {
            sobel_req.mode = SobelFrameRequest::Mode::SelectionGold;
            sobel_req.instance_indices = { selected_instance };
            want_sobel = true;
        }
        else if (visualize_confirmed_tips_ && !sobel_incomplete_hashes_.empty())
        {
            sobel_req.mode = SobelFrameRequest::Mode::IncompleteTraceOrange;
            sobel_req.instance_indices = resolve_hashes(sobel_incomplete_hashes_, 32);
            want_sobel = !sobel_req.instance_indices.empty();
        }
        else if (visualize_confirmed_tips_ && !sobel_tip_hashes_.empty())
        {
            // One frontier tip per lane (presenter), not whole confirmed spine.
            sobel_req.mode = SobelFrameRequest::Mode::ConfirmedTipsGreen;
            sobel_req.instance_indices = resolve_hashes(sobel_tip_hashes_, 16);
            want_sobel = !sobel_req.instance_indices.empty();
        }
    }

    VkCommandBuffer commandBuffer = inFlightFrames[currentFrame].commandBuffer;
    vkResetCommandBuffer(commandBuffer, 0);
    record_command_buffer(commandBuffer, acq.image_index, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                          /*defer_present=*/want_sobel);

    if (want_sobel)
    {
        submit_frame_with_async_sobel(begin.frame_index, acq.image_index, commandBuffer,
                                      acq.image_available, acq.render_finished, sobel_req);
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

void VulkanEngine::submit_frame_with_async_sobel(uint32_t frame_index, uint32_t image_index,
                                                 VkCommandBuffer graphics_cb,
                                                 VkSemaphore image_available,
                                                 VkSemaphore render_finished,
                                                 const SobelFrameRequest& req)
{
    // Wait for prior Sobel chain: shared sel_depth/edge must not be rewritten while sampled.
    if (sobel_fence_in_flight_ && sobel_done_fence_ != VK_NULL_HANDLE)
    {
        vkWaitForFences(device, 1, &sobel_done_fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &sobel_done_fence_);
        sobel_fence_in_flight_ = false;
    }

    // Slot is idle: FramePresenter::begin already wait_frame(frame_index) for this timeline slot.
    FramesInFlight& slot = inFlightFrames[frame_index % MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer compute_cb = slot.computeCommandBuffer;
    VkCommandBuffer overlay_cb = slot.overlayCommandBuffer;
    const VkSemaphore g2c = sobel_.graphics_to_compute(frame_index);
    const VkSemaphore cfin = sobel_.compute_finished(frame_index);

    // After main+pick: depth-only redraw of requested instances into sel_depth_,
    // then release that depth for async CMP Sobel (scene depth is not used).
    {
        SelectionDepthDrawParams sp{};
        sp.cmd = graphics_cb;
        sp.ubo_set = frame_descriptors_.set();
        sp.vertex_buffer = frame_resources_.vertex_buffer();
        sp.instance_buffer = frame_resources_.instance_buffer();
        sp.index_buffer = frame_resources_.index_buffer();
        sp.instance_indices = req.instance_indices.data();
        sp.instance_index_count = static_cast<uint32_t>(req.instance_indices.size());
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
        sig.semaphore = g2c;
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
        vkResetCommandBuffer(compute_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(compute_cb, &bi);

        sobel_.record_dispatch(compute_cb, /*strength=*/14.0f, /*threshold=*/0.001f);
        vkEndCommandBuffer(compute_cb);

        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = compute_cb;
        VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        wait.semaphore = g2c;
        wait.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        VkSemaphoreSubmitInfo sig{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        sig.semaphore = cfin;
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
        vkResetCommandBuffer(overlay_cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(overlay_cb, &bi);

        sobel_.record_edge_acquire_for_graphics(overlay_cb);

        // Color still COLOR_ATTACHMENT — load and draw edges (1× resolved swapchain)
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
        vkCmdBeginRendering(overlay_cb, &ri);

        static const float kGoldHighlight[4]   = { 1.0f, 0.85f, 0.15f, 1.35f };
        static const float kGreenHighlight[4]  = { 0.25f, 0.95f, 0.40f, 1.35f };
        static const float kOrangeHighlight[4] = { 1.0f, 0.45f, 0.08f, 1.35f };
        const float* highlight = kGoldHighlight;
        if (req.mode == SobelFrameRequest::Mode::ConfirmedTipsGreen)
            highlight = kGreenHighlight;
        else if (req.mode == SobelFrameRequest::Mode::IncompleteTraceOrange)
            highlight = kOrangeHighlight;
        sobel_.record_overlay(overlay_cb, width, height, highlight);
        vkCmdEndRendering(overlay_cb);

        frame_recorder_.transition_color_to_present(overlay_cb, swapchainImages[image_index]);
        vkEndCommandBuffer(overlay_cb);

        const uint64_t signal_value = frame_presenter_.frame_counter() + 1;
        VkCommandBufferSubmitInfo cb{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cb.commandBuffer = overlay_cb;
        VkSemaphoreSubmitInfo waits[1]{};
        waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waits[0].semaphore = cfin;
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
        if (vkQueueSubmit2(queues_.get(QueueType::_3D), 1, &submit, sobel_done_fence_) != VK_SUCCESS)
            throw std::runtime_error("async sobel: overlay submit failed");
        sobel_fence_in_flight_ = true;

        VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        vkQueuePresentKHR(queues_.get(QueueType::_3D), &present);

        frame_sync_.set_frame_timeline_value(frame_index, signal_value);
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
        pr.depth_format = swapchain_targets_.depth_format();
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
    info.buffer_manager = &buffer_manager_;
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


void VulkanEngine::create_sync_objects()
{
    FrameSyncCreateInfo info{};
    info.device = device;
    info.frames_in_flight = MAX_FRAMES_IN_FLIGHT;
    info.swapchain_image_count = MAX_SWAPCHAIN_IMAGES;
    frame_sync_.create(info);

    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device, &fci, nullptr, &sobel_done_fence_) != VK_SUCCESS)
        throw std::runtime_error("Failed to create sobel completion fence");
    sobel_fence_in_flight_ = false;
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
    publish_frame(frame, {}, {}, {});
}

void VulkanEngine::publish_frame(const FrameSubmit& frame,
                                 const std::vector<std::string>& pick_map,
                                 const std::vector<std::string>& confirmed_tip_hashes,
                                 const std::vector<std::string>& incomplete_trace_hashes)
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
    slot.confirmed_tip_hashes = confirmed_tip_hashes;
    slot.incomplete_trace_hashes = incomplete_trace_hashes;

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
    sobel_tip_hashes_ = slot.confirmed_tip_hashes;
    sobel_incomplete_hashes_ = slot.incomplete_trace_hashes;
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
    const bool msaa = swapchain_targets_.msaa_enabled();
    rp.color_image = msaa ? swapchain_targets_.msaa_color_image() : swapchainImages[imageIndex];
    rp.color_view = msaa ? swapchain_targets_.msaa_color_view()
                         : swapchain_targets_.color_view(imageIndex);
    rp.depth_image = swapchain_targets_.depth_image();
    rp.depth_view = swapchain_targets_.depth_view();
    rp.samples = swapchain_targets_.sample_count();
    rp.resolve_color_view = msaa ? swapchain_targets_.color_view(imageIndex) : VK_NULL_HANDLE;
    rp.resolve_color_image = msaa ? swapchainImages[imageIndex] : VK_NULL_HANDLE;
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

        // Short RMB reset (look home + pan origin) is in BlockflowOverlay.
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
            PickerRecordParams pick{};
            pick.cmd = buffer;
            pick.mouse_x = static_cast<uint32_t>(mx);
            pick.mouse_y = static_cast<uint32_t>(my);
            pick.width = width;
            pick.height = height;
            pick.viewport_extent = swapchainExtent;
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
    if (sobel_done_fence_ != VK_NULL_HANDLE)
    {
        if (sobel_fence_in_flight_)
            vkWaitForFences(device, 1, &sobel_done_fence_, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, sobel_done_fence_, nullptr);
        sobel_done_fence_ = VK_NULL_HANDLE;
        sobel_fence_in_flight_ = false;
    }
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
