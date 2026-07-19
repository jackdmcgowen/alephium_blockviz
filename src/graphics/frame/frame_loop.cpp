#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/frame/frame_shared_state.hpp"
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
#include "imgui_impl_win32.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include "imgui_impl_vulkan.h"
#include <windows.h>

void GraphicsSystem::render_loop()
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

        g_debugDrawer.clear();

        std::string selected_hash_local;
        std::string hovered_hash_local;
        std::string ui_dep_hover_local;
        bool filter_multi_tx_local = false;
        AlphBlock selected_detail_local;

        {
            std::lock_guard<std::mutex> slock(selection_mutex_);
            selected_hash_local = selected_hash_;
            hovered_hash_local = hovered_hash_;
            ui_dep_hover_local = ui_dep_hover_hash_;
            filter_multi_tx_local = filter_multi_tx_;
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
            fin.selected_detail = selected_detail_local;
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
                frame_frustum = camera_->frustum();
                fin.frustum = &frame_frustum;
                fin.camera_eye = camera_->camera().eye;
                fin.has_camera_eye = true;
                if (scene_)
                    scene_->set_camera_scroll_z(camera_->scroll_z());
            }

            frame_source_->prepare(fin, fout, &g_debugDrawer);

            if (camera_)
            {
                // Dynamic near/far from visible segment span (presenter suggestion).
                // Applied after prepare so this frame's UBO matches draw-set depth;
                // next frame's cull frustum picks up the tighter clip.
                if (fout.has_clip_suggestion)
                {
                    const float n = std::max(0.5f, fout.suggested_near_z);
                    const float f = std::max(n + 10.f, fout.suggested_far_z);
                    camera_->set_clip(n, f);
                }
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
                          fout.cyan_frontier_hashes, fout.incomplete_hashes);

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
            publish_frame(submit, frame_pick_map, {}, {}, {});
            frame_ui.selected_hash = selected_hash_local;
            frame_ui.selected_detail = selected_detail_local;
            frame_ui.seq = submit_seq_;
        }

        publish_ui_snapshot(std::move(frame_ui));
        apply_published_frame();

        // F12 → screenshot of full client (scene + ImGui).
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false))
            request_screenshot(nullptr);

        if (overlay_)
            overlay_->draw();

        ImGui::Render();
        render();
        // Capture after present path paints ImGui (end of frame content).
        consume_and_save_screenshot_();

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


void GraphicsSystem::render()
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

        auto resolve_hashes = [&](const std::vector<std::string>& hashes) {
            std::vector<uint32_t> idxs;
            idxs.reserve(hashes.size());
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
                if (idxs.size() >= kMaxSobelInstances)
                    break;
            }
            return idxs;
        };

        // Cubes only. Multi-layer: gold exclusive; else orange then cyan then green (all co-visible).
        // Cyan = frontier children; green = confirmed frontier tips (H_c).
        if (selected_instance != ~0u)
        {
            SobelFrameRequest::Layer layer;
            layer.mode = SobelFrameRequest::Mode::SelectionGold;
            layer.instance_indices = { selected_instance };
            sobel_req.layers.push_back(std::move(layer));
            want_sobel = true;
        }
        else if (visualize_confirmed_tips_)
        {
            auto push_layer = [&](SobelFrameRequest::Mode mode,
                                  const std::vector<std::string>& hashes) {
                auto idxs = resolve_hashes(hashes);
                if (idxs.empty())
                    return;
                SobelFrameRequest::Layer layer;
                layer.mode = mode;
                layer.instance_indices = std::move(idxs);
                sobel_req.layers.push_back(std::move(layer));
            };
            // Paint order low â†’ high: orange under cyan under green.
            push_layer(SobelFrameRequest::Mode::IncompleteTraceOrange, sobel_incomplete_hashes_);
            push_layer(SobelFrameRequest::Mode::CyanFrontier, sobel_cyan_hashes_);
            push_layer(SobelFrameRequest::Mode::ConfirmedTipsGreen, sobel_tip_hashes_);
            want_sobel = !sobel_req.layers.empty();
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



