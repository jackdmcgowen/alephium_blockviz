#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/frame/frame_shared_state.hpp"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/debug/debug_drawer.h"
#include "domain/alph_block.hpp"
#include "app/ui_chrome.hpp"

#include <chrono>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "graphics/platform/gfx_platform.hpp"

namespace
{
const char* bound_label(FrameBoundClass b)
{
    switch (b)
    {
    case FrameBoundClass::Cpu:         return "CPU";
    case FrameBoundClass::Gpu:         return "GPU";
    case FrameBoundClass::PresentSync: return "PRESENT/SYNC";
    default:                          return "unknown";
    }
}

void draw_profiler_hud(const FrameTimingSnapshot& snap)
{
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::SetNextWindowPos(ImVec2(12.f, 12.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Frame profiler (F3)", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }
    if (!snap.valid)
    {
        ImGui::TextUnformatted("Waiting for samples…");
        ImGui::End();
        return;
    }
    ImGui::Text("frame %.2f ms | CPU %.2f | GPU %.2f | bound %s",
                snap.frame_ms, snap.cpu_ms, snap.gpu_ms, bound_label(snap.bound));
    ImGui::Text("samples %llu", static_cast<unsigned long long>(snap.sample_index));
    if (ImGui::BeginTable("scopes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("scope");
        ImGui::TableSetupColumn("cpu");
        ImGui::TableSetupColumn("gpu");
        ImGui::TableSetupColumn("med cpu");
        ImGui::TableSetupColumn("med gpu");
        ImGui::TableHeadersRow();
        for (uint32_t i = 0; i < snap.scope_count; ++i)
        {
            const FrameTimingScope& s = snap.scopes[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s.cpu_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s.gpu_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s.cpu_median_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s.gpu_median_ms);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
} // namespace

void GraphicsSystem::render_loop()
{
    const double frameTimeMin = 1000.0 / 60; // ~16.67ms for 60Hz
    double t = 0.0;
    double dt = 0.0;

    while (running)
    {
        ImGui_ImplVulkan_NewFrame();
        gfx_platform_imgui_new_frame();
        ImGui::NewFrame();

        const auto t1 = std::chrono::steady_clock::now();

        // Host-frame wall for profiler starts here (paired with end_host_frame).
        g_debugDrawer.clear();

        std::string selected_hash_local;
        std::string hovered_hash_local;
        std::string ui_dep_hover_local;
        bool filter_multi_tx_local = false;
        double filter_min_alph_local = 0.0;
        AlphBlock selected_detail_local;

        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            selected_hash_local = selected_hash_;
            hovered_hash_local = hovered_hash_;
            ui_dep_hover_local = ui_dep_hover_hash_;
            filter_multi_tx_local = filter_multi_tx_;
            filter_min_alph_local = filter_min_alph_;
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
            ui_dep_hover_local = ui_dep_hover_hash_;
            filter_multi_tx_local = filter_multi_tx_;
            filter_min_alph_local = filter_min_alph_;
            selected_detail_local = selected_block;
        }

        UiSnapshot frame_ui{};
        std::vector<std::string> frame_pick_map;

        // Camera: aspect â†’ look-aim â†’ tick â†’ frustum (before geometry submit).
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
            fin.ui_dep_hover_hash = ui_dep_hover_local;
            fin.filter_txn_gt_1 = filter_multi_tx_local;
            if (filter_min_alph_local > 0.0)
                fin.filter_min_alph_atto = alph_from_double_to_atto(filter_min_alph_local);
            fin.selected_detail = selected_detail_local;
            fin.enable_role_outlines = visualize_confirmed_tips_;
            // Half-extents for unit cube mesh (±1); slight inflate avoids edge pop.
            fin.instance_half_extents = glm::vec3(1.05f);

            // Look target from layout is still needed for aim; run a thin prepare after
            // tick if we need positions â€” look aim uses fout after prepare below.
            // Tick camera first without new aim so frustum matches previous look;
            // then set aim when selection changes (target from prepare).

            FrameSourceOutput fout{};
            // Host ScenePresenter locks scene; must not hold scene mutex here.
            // First prepare pass needs frustum â€” tick camera, then cull.
            Frustum frame_frustum{};
            if (camera_)
            {
                // Timeline Z: live tip at "now" with ms resolution (not time()*1000 —
                // second steps made the attached follow jump/jitter every 1 m).
                const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count();
                int64_t origin_ms = scene_ ? scene_->timeline_origin_ms() : 0;
                if (origin_ms <= 0)
                    origin_ms = now_ms - static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
                const int64_t genesis_ms =
                    scene_ ? scene_->genesis_ms() : ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
                constexpr float mps = 1.f;
                const float live_z =
                    -static_cast<float>(now_ms - origin_ms) * 0.001f * mps;
                const float past_z =
                    -static_cast<float>(genesis_ms - origin_ms) * 0.001f * mps;
                const float z_lo = std::min(live_z, past_z) - 120.f;
                const float z_hi = std::max(live_z, past_z) + 120.f;
                camera_->set_scroll_z_limits(z_lo, z_hi);
                camera_->set_live_scroll_z(live_z);

                camera_->tick(last_frame_dt_sec_);

                // Same-frame cull: set near/far from eye + ring span BEFORE frustum
                // so prepare() culls with the same clip this frame's UBO will use.
                {
                    const float eye_z = camera_->scroll_z();
                    const int64_t window_ms =
                        static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
                    const int G_live =
                        window_ms > 0 && now_ms > genesis_ms
                            ? static_cast<int>((now_ms - genesis_ms) / window_ms)
                            : 0;
                    const float older_sec = eye_z - live_z;
                    int cam_k = 0;
                    if (older_sec >= 1.f && window_ms > 0)
                        cam_k = static_cast<int>(
                            older_sec / (static_cast<float>(window_ms) * 0.001f));
                    cam_k = std::clamp(cam_k, 0, std::max(0, G_live));
                    float vis_lo = 0.f, vis_hi = 0.f;
                    bool have = false;
                    float seg_w = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS) * mps;
                    // Match presenter render ring: centered ±ALPH_RENDER_RING_HALF.
                    for (int d = -ALPH_RENDER_RING_HALF; d <= ALPH_RENDER_RING_HALF; ++d)
                    {
                        const int k = cam_k + d;
                        if (k < 0 || k > G_live)
                            continue;
                        const int G = std::max(0, G_live - k);
                        int64_t from_ms = genesis_ms + static_cast<int64_t>(G) * window_ms;
                        int64_t to_ms = from_ms + window_ms;
                        if (k == 0 && now_ms < to_ms)
                            to_ms = std::max(from_ms + 1, now_ms);
                        const float z0 =
                            -static_cast<float>(from_ms - origin_ms) * 0.001f * mps;
                        const float z1 =
                            -static_cast<float>(to_ms - origin_ms) * 0.001f * mps;
                        const float zn = std::min(z0, z1);
                        const float zo = std::max(z0, z1);
                        if (!have)
                        {
                            vis_lo = zn;
                            vis_hi = zo;
                            have = true;
                            seg_w = std::max(1.f, zo - zn);
                        }
                        else
                        {
                            vis_lo = std::min(vis_lo, zn);
                            vis_hi = std::max(vis_hi, zo);
                        }
                    }
                    constexpr float kHardFarCap = 20000.f;
                    constexpr float kMinNear = 0.5f;
                    constexpr float kLayoutR = 20.f * 1.35f;
                    float near_z = kMinNear;
                    float far_z = Camera::kDefaultFarZ;
                    if (have)
                    {
                        const float z_dist = std::max(std::abs(vis_hi - eye_z),
                                                      std::abs(vis_lo - eye_z));
                        const float pad = std::max(seg_w * 0.35f, 40.f);
                        far_z = std::min(
                            kHardFarCap,
                            std::max(near_z + 50.f, z_dist + kLayoutR + 40.f + pad));
                    }
                    camera_->set_clip(near_z, far_z);
                }

                frame_frustum = camera_->frustum();
                fin.frustum = &frame_frustum;
                fin.camera_eye = camera_->camera().eye;
                fin.has_camera_eye = true;
                if (scene_)
                    scene_->set_camera_scroll_z(camera_->scroll_z());
            }

            {
                auto prep = frame_profiler_.cpu_scope("Prepare");
                frame_source_->prepare(fin, fout, &g_debugDrawer);
            }

            if (camera_)
            {
                // Refine clip from presenter if present (matches prepass in common cases).
                if (fout.has_clip_suggestion)
                {
                    const float n = std::max(0.5f, fout.suggested_near_z);
                    const float f = std::max(n + 10.f, fout.suggested_far_z);
                    camera_->set_clip(n, f);
                }
                if (fout.has_look_target && selected_hash_local != camera_->look_aim_hash())
                    camera_->set_look_target(fout.look_target_pos, selected_hash_local);
                else if (selected_hash_local.empty() && camera_->look_engaged())
                    camera_->release_look_aim();
                // Re-tick look slerp after aim change for this frame's UBO.
                camera_->tick(0.f);
            }

            FrameSubmit submit{};
            submit.instances = fout.instances.empty() ? nullptr : fout.instances.data();
            submit.instance_count = fout.instances.size();
            submit.camera = camera_ ? camera_->ubo() : CameraUBO{};
            submit.client_seq = ++submit_seq_;
            {
                auto pub = frame_profiler_.cpu_scope("PublishUpload");
                publish_frame(submit, fout.pick_map, fout.sobel_outlines);
            }

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
            // Empty frame-source: clear outlines for this frame (paired with empty pick_map).
            publish_frame(submit, frame_pick_map, {});
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
        }

        publish_ui_snapshot(std::move(frame_ui));
        apply_published_frame();

        // F12 → screenshot of full client (scene + ImGui).
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
            request_screenshot(nullptr);

        // F3 → toggle frame profiler HUD (+ enable sampling when shown).
        if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
        {
            profiler_hud_ = !profiler_hud_;
            if (profiler_hud_ && !frame_profiler_.enabled())
                frame_profiler_.set_enabled(true);
            else if (!profiler_hud_)
                frame_profiler_.set_enabled(false);
        }

        if (overlay_)
        {
            auto ui = frame_profiler_.cpu_scope("OverlayUi");
            overlay_->draw();
        }

        if (profiler_hud_ && frame_profiler_.enabled())
        {
            FrameTimingSnapshot snap{};
            frame_profiler_.copy_snapshot(snap);
            draw_profiler_hud(snap);
        }

        ImGui::Render();
        {
            auto sub = frame_profiler_.cpu_scope("SubmitPresent");
            render();
        }
        // Capture after present path paints ImGui (end of frame content).
        consume_and_save_screenshot_();

        // Work wall (before 60 Hz pad) — used for profiler frame_ms / bound class.
        const auto t_work = std::chrono::steady_clock::now();
        const float work_ms = std::chrono::duration<float, std::milli>(t_work - t1).count();
        frame_profiler_.end_host_frame(work_ms);

        do
        {
            const auto t2 = std::chrono::steady_clock::now();
            dt = std::chrono::duration<double, std::milli>(t2 - t1).count();
            t += dt;
        } while (dt <= frameTimeMin);

        last_frame_dt_sec_ = static_cast<float>(dt) * 0.001f;
        if (last_frame_dt_sec_ < 1e-4f || last_frame_dt_sec_ > 0.1f)
            last_frame_dt_sec_ = 1.f / 60.f;
    }

}   /* render_loop() */


void GraphicsSystem::render()
{
    std::lock_guard<std::mutex> lk(renderMutex);

    const FramePresenter::BeginResult begin =
        frame_presenter_.begin(device, frame_sync_, MAX_FRAMES_IN_FLIGHT);
    currentFrame = static_cast<int>(begin.frame_index);

    if (begin.run_deferred_resize)
        resize_internal();

    // After wait_frame for this slot: resolve prior GPU timestamps, then arm recording.
    if (frame_profiler_.enabled())
    {
        frame_profiler_.on_frame_slot_ready(device, static_cast<uint32_t>(currentFrame));
        frame_profiler_.begin_record(static_cast<uint32_t>(currentFrame));
    }

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

    // App builds colored outline list; graphics draws all in one pass (no role names).
    const bool want_sobel =
        sobel_pipe_.ready() && frame_resources_.outline_count() > 0;

    VkCommandBuffer commandBuffer = inFlightFrames[currentFrame].commandBuffer;
    vkResetCommandBuffer(commandBuffer, 0);
    {
        auto rec = frame_profiler_.cpu_scope("RecordMain");
        record_command_buffer(commandBuffer, acq.image_index, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                              /*defer_present=*/want_sobel);
    }

    if (want_sobel)
    {
        FramesInFlight& slot = inFlightFrames[begin.frame_index % MAX_FRAMES_IN_FLIGHT];
        SobelAsyncSubmitContext sctx{};
        sctx.device = device;
        sctx.graphics_queue = queues_.get(QueueType::_3D);
        sctx.compute_queue = queues_.get(QueueType::CMP);
        sctx.width = width;
        sctx.height = height;
        sctx.main_graphics_cb = commandBuffer;
        sctx.compute_cb = slot.computeCommandBuffer;
        sctx.overlay_cb = slot.overlayCommandBuffer;
        sctx.image_available = acq.image_available;
        sctx.render_finished = acq.render_finished;
        sctx.frame_ubo_set = frame_descriptors_.set();
        sctx.vertex_buffer = frame_resources_.vertex_buffer();
        sctx.outline_instance_buffer = frame_resources_.outline_instance_buffer();
        sctx.index_buffer = frame_resources_.index_buffer();
        sctx.outline_count = static_cast<uint32_t>(frame_resources_.outline_count());
        sctx.index_count = 36;
        sctx.swapchain_color_view = swapchain_targets_.color_view(acq.image_index);
        sctx.swapchain_image = swapchainImages[acq.image_index];
        sctx.swapchain = swapchain;
        sctx.recorder = &frame_recorder_;
        sctx.frame_sync = &frame_sync_;
        sctx.presenter = &frame_presenter_;
        sctx.profiler = frame_profiler_.enabled() ? &frame_profiler_ : nullptr;
        sobel_async_.submit(sobel_pipe_, sctx, begin.frame_index, acq.image_index);
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



