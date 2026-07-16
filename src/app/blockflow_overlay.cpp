#include "app/blockflow_overlay.hpp"
#include "app/ui_chrome.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <glm/glm.hpp>

// Shard colors for feed / inspector labels (UX only — not engine)
static const glm::vec3 kShardColors[16] = {
    glm::vec3(1.00f, 0.34f, 0.20f),
    glm::vec3(0.20f, 1.00f, 0.34f),
    glm::vec3(0.20f, 0.34f, 1.00f),
    glm::vec3(1.00f, 0.20f, 1.00f),
    glm::vec3(1.00f, 0.76f, 0.00f),
    glm::vec3(0.85f, 0.97f, 0.65f),
    glm::vec3(0.78f, 0.00f, 0.22f),
    glm::vec3(0.34f, 0.09f, 0.27f),
    glm::vec3(1.00f, 1.00f, 1.00f),
    glm::vec3(0.50f, 0.50f, 0.00f),
    glm::vec3(0.00f, 1.00f, 1.00f),
    glm::vec3(1.00f, 0.75f, 0.80f),
    glm::vec3(0.50f, 0.00f, 0.50f),
    glm::vec3(1.00f, 1.00f, 0.00f),
    glm::vec3(0.50f, 0.50f, 0.50f),
    glm::vec3(0.00f, 1.00f, 0.00f)
};

BlockflowOverlay::BlockflowOverlay(CameraController& camera, IEngine& engine)
    : camera_(camera)
    , engine_(engine)
{
    session_start_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
}

void BlockflowOverlay::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt_sec = (io.DeltaTime > 0.f) ? io.DeltaTime : (1.f / 60.f);

    const float ui_w = io.DisplaySize.x;
    const float ui_h = io.DisplaySize.y;
    const float inspector_w = ui_chrome::inspector_width(ui_w);
    const float toolbar_h = ui_chrome::kToolbarHeight;
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    const bool over_scene =
        !io.WantCaptureMouse &&
        mx >= 0.f && my >= 0.f &&
        mx < ui_w - inspector_w &&
        my < ui_h - toolbar_h;

    // Camera motion — Z-track (keys + wheel)
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
            camera_.nudge_scroll(CameraController::kEyeZStep * dt_sec);
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
            camera_.nudge_scroll(-CameraController::kEyeZStep * dt_sec);
    }
    // Positive MouseWheel = scroll up → +scroll_z (matches Up arrow).
    if (over_scene && io.MouseWheel != 0.f)
        camera_.nudge_scroll(io.MouseWheel * CameraController::kWheelStep);

    constexpr float kDragThresholdPx = 4.f;

    // Left-hold drag: free look. Short LMB click still picks (engine).
    if (over_scene && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        lmb_down_over_scene_ = true;
        lmb_dragged_ = false;
        lmb_drag_dist_px_ = 0.f;
    }
    if (lmb_down_over_scene_ && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float dx = io.MouseDelta.x;
        const float dy = io.MouseDelta.y;
        lmb_drag_dist_px_ += std::sqrt(dx * dx + dy * dy);
        if (lmb_drag_dist_px_ >= kDragThresholdPx)
            lmb_dragged_ = true;
        if (lmb_dragged_ && (dx != 0.f || dy != 0.f))
            camera_.add_look_delta(dx, dy);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        lmb_down_over_scene_ = false;
        lmb_dragged_ = false;
        lmb_drag_dist_px_ = 0.f;
    }

    // Right-hold drag: pan. Short RMB click: clear selection + home look + pan to origin.
    if (over_scene && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        rmb_down_over_scene_ = true;
        rmb_dragged_ = false;
        rmb_drag_dist_px_ = 0.f;
    }
    if (rmb_down_over_scene_ && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const float dx = io.MouseDelta.x;
        const float dy = io.MouseDelta.y;
        rmb_drag_dist_px_ += std::sqrt(dx * dx + dy * dy);
        if (rmb_drag_dist_px_ >= kDragThresholdPx)
            rmb_dragged_ = true;
        if (rmb_dragged_ && (dx != 0.f || dy != 0.f))
            camera_.add_pan_delta(dx, dy);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (rmb_down_over_scene_ && !rmb_dragged_)
            engine_.clear_selection(); // clear_look_target: home look + pan origin
        rmb_down_over_scene_ = false;
        rmb_dragged_ = false;
        rmb_drag_dist_px_ = 0.f;
    }

    const UiSnapshot ui = engine_.copy_ui_snapshot();

    draw_toolbar(ui, ui_w, ui_h);
    draw_inspector(ui, ui_w, ui_h);
}

void BlockflowOverlay::draw_toolbar(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float inspector_w = ui_chrome::inspector_width(ui_w);
    const float toolbar_h = ui_chrome::kToolbarHeight;
    const float scene_w = std::max(1.f, ui_w - inspector_w);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(0, ui_h - toolbar_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(scene_w, toolbar_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 8.f));
    ImGui::Begin("Blockflow", nullptr, flags);

    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    const float elapsed_ms = static_cast<float>(now - session_start_ms_) + 1e-3f;
    const float bps = ui.total_blocks / (0.001f * elapsed_ms);

    // True eye Z (manual scroll only — no auto-scroll rate).
    ImGui::Text("z: %.1f  (Up/Down / wheel)", camera_.camera().eye.z);
    ImGui::SameLine(0.f, 24.f);
    ImGui::Text("blocks: %d", ui.total_blocks);
    ImGui::SameLine(0.f, 24.f);
    ImGui::Text("rate: %1.2f/s", bps);
    ImGui::SameLine(0.f, 24.f);
    ImGui::Text("frontier %d / live tips %d", ui.confirmed_tip_count, ui.tip_count);

    // Per-chain highest sequential confirmed height (H_c).
    ImGui::TextDisabled("confirmed H (from->to):");
    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
        {
            const int lane = f * ALPH_NUM_GROUPS + t;
            const int hc = ui.confirmed_height_by_lane[lane];
            if (t > 0)
                ImGui::SameLine(0.f, 10.f);
            if (hc < 0)
                ImGui::TextDisabled("%d->%d:—", f, t);
            else
                ImGui::Text("%d->%d:%d", f, t, hc);
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("feed", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    int feed_i = 0;
    for (const FeedEntry& entry : ui.feed)
    {
        // Index + hash: feed can list the same hash more than once.
        ImGui::PushID(feed_i++);
        ImGui::PushID(entry.hash.c_str());
        const int shardId = entry.chain_idx() & 15;
        ImGui::TextColored(
            ImVec4(kShardColors[shardId].r, kShardColors[shardId].g, kShardColors[shardId].b, 1.0f),
            "[%d->%d]", entry.chainFrom, entry.chainTo);
        ImGui::SameLine();
        if (ImGui::Button(entry.hash.c_str()))
        {
            engine_.set_selection(entry.hash);
            ImGui::SetClipboardText(entry.hash.c_str());
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

namespace
{
// Compact hash for tree labels (full id still available via link when expanded).
void short_id(const std::string& s, char* buf, size_t buf_n)
{
    if (s.empty())
    {
        snprintf(buf, buf_n, "—");
        return;
    }
    if (s.size() <= 14)
    {
        snprintf(buf, buf_n, "%s", s.c_str());
        return;
    }
    snprintf(buf, buf_n, "%.6s…%.4s", s.c_str(), s.c_str() + s.size() - 4);
}

void explorer_url(char* buf, size_t n, const char* kind, const std::string& id)
{
    snprintf(buf, n, "https://explorer.alephium.org/%s/%s", kind, id.c_str());
}

// One UTXO row: link + amount (collapsed lists stay one line).
void draw_utxo_row(UTXO& u, const char* kind, int index)
{
    ImGui::PushID(index);
    char url[512];
    char label[96];
    short_id(u.address, label, sizeof(label));
    explorer_url(url, sizeof(url), "addresses", u.address);

    ImGui::Bullet();
    ImGui::TextDisabled("%s", kind);
    ImGui::SameLine();
    ImGui::TextLinkOpenURL(u.address.empty() ? "##addr" : label, url);
    if (ImGui::IsItemHovered() && !u.address.empty())
        ImGui::SetTooltip("%s", u.address.c_str());
    ImGui::SameLine();
    ImGui::Text("%s ALPH", u.toAmount().c_str());
    if (!u.key.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("key: %s\nhint: %d", u.key.c_str(), u.hint);
    ImGui::PopID();
}
} // namespace

void BlockflowOverlay::draw_inspector(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float inspector_w = ui_chrome::inspector_width(ui_w);

    // Prefer live selection detail if feed click updated selection this frame
    AlphBlock inspector = ui.selected_detail;
    {
        AlphBlock live = engine_.copy_selected_block();
        if (!live.hash.empty() &&
            (inspector.hash != live.hash || inspector.txns.empty()))
            inspector = std::move(live);
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::SetNextWindowPos(ImVec2(ui_w - inspector_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(inspector_w, ui_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Block", nullptr, flags);

    char url[512];
    char id_buf[64];
    if (!inspector.hash.empty())
    {
        explorer_url(url, sizeof(url), "blocks", inspector.hash);
        short_id(inspector.hash, id_buf, sizeof(id_buf));

        // ── Block header (always open, compact) ──
        ImGui::TextDisabled("hash");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL(id_buf, url);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n(click opens explorer)", inspector.hash.c_str());

        const int shardId = inspector.chain_idx() & 15;
        ImGui::Text("height %d", inspector.height);
        ImGui::SameLine(0.f, 16.f);
        ImGui::TextColored(
            ImVec4(kShardColors[shardId].r, kShardColors[shardId].g, kShardColors[shardId].b, 1.0f),
            "chain [%d→%d]", inspector.chainFrom, inspector.chainTo);

        if (inspector.timestamp > 0)
        {
            const time_t t = static_cast<time_t>(inspector.timestamp / 1000);
            char tbuf[40];
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            ImGui::TextDisabled("time %s", tbuf);
        }

        ImGui::TextDisabled("%d tx · %d deps · %d uncles",
                            static_cast<int>(inspector.txns.size()),
                            static_cast<int>(inspector.deps.size()),
                            static_cast<int>(inspector.uncles.size()));

        // ── Dependencies (collapsed by default) ──
        if (!inspector.deps.empty() &&
            ImGui::TreeNodeEx("##deps", ImGuiTreeNodeFlags_SpanAvailWidth,
                              "Deps (%d)", static_cast<int>(inspector.deps.size())))
        {
            int di = 0;
            for (const std::string& dep : inspector.deps)
            {
                ImGui::PushID(di++);
                short_id(dep, id_buf, sizeof(id_buf));
                explorer_url(url, sizeof(url), "blocks", dep);
                ImGui::Bullet();
                ImGui::TextLinkOpenURL(dep.empty() ? "##dep" : id_buf, url);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", dep.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        // ── Transactions: one-line headers, expand for detail ──
        ImGui::Text("Transactions (%d)", static_cast<int>(inspector.txns.size()));
        if (inspector.txns.empty())
            ImGui::TextDisabled("No transactions loaded (detail may still be fetching).");

        // Lightweight: only clip closed rows when many txs (open rows still expand below).
        const int txn_count = static_cast<int>(inspector.txns.size());
        for (int tx_i = 0; tx_i < txn_count; ++tx_i)
        {
            AlphTxn& tx = inspector.txns[static_cast<size_t>(tx_i)];
            ImGui::PushID(tx_i);
            ImGui::PushID(tx.txid.c_str());

            short_id(tx.txid, id_buf, sizeof(id_buf));
            const int n_in = static_cast<int>(tx.inputs.size());
            const int n_out = static_cast<int>(tx.outputs.size());

            // Closed label: short id + io counts + gas (one line, no heavy widgets).
            const bool open = ImGui::TreeNodeEx(
                "##tx",
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap,
                "%s  in %d · out %d · gas %d",
                id_buf, n_in, n_out, tx.gasAmount);

            if (ImGui::IsItemHovered() && !tx.txid.empty())
                ImGui::SetTooltip("%s", tx.txid.c_str());

            if (open)
            {
                explorer_url(url, sizeof(url), "transactions", tx.txid);
                ImGui::TextDisabled("txid");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(tx.txid.empty() ? "##txid" : id_buf, url);
                if (ImGui::IsItemHovered() && !tx.txid.empty())
                    ImGui::SetTooltip("%s", tx.txid.c_str());

                ImGui::Text("v%d  net %d  gas %d", tx.version, tx.networkId, tx.gasAmount);
                if (!tx.gasPrice.empty())
                {
                    ImGui::SameLine(0.f, 12.f);
                    ImGui::TextDisabled("price %s", tx.gasPrice.c_str());
                }
                if (!tx.scriptOpt.empty())
                {
                    ImGui::TextDisabled("script");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", tx.scriptOpt.c_str());
                }

                // Nested collapsible I/O (closed by default).
                if (n_in > 0 &&
                    ImGui::TreeNodeEx("##inputs", ImGuiTreeNodeFlags_SpanAvailWidth,
                                      "Inputs (%d)", n_in))
                {
                    for (int i = 0; i < n_in; ++i)
                        draw_utxo_row(tx.inputs[static_cast<size_t>(i)], "in", i);
                    ImGui::TreePop();
                }
                if (n_out > 0 &&
                    ImGui::TreeNodeEx("##outputs", ImGuiTreeNodeFlags_SpanAvailWidth,
                                      "Outputs (%d)", n_out))
                {
                    for (int i = 0; i < n_out; ++i)
                        draw_utxo_row(tx.outputs[static_cast<size_t>(i)], "out", i);
                    ImGui::TreePop();
                }
                if (n_in == 0 && n_out == 0)
                    ImGui::TextDisabled("No inputs/outputs in payload.");

                ImGui::TreePop();
            }

            ImGui::PopID();
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextWrapped(
            "Select a block from the feed below or click a cube in the scene.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Camera: wheel/arrows Z · LMB-drag look · short LMB pick · RMB-drag pan · short RMB reset");
        ImGui::TextDisabled(
            "Solid=main+deps · green=frontier tip · orange=missing dep · cyan=live tip · gold=select");
        {
            const char* pname = "Bootstrap";
            if (ui.trace_phase == 1) pname = "IdentifyTips";
            else if (ui.trace_phase == 2) pname = "DfsTrace";
            else if (ui.trace_phase == 3) pname = "Steady";
            ImGui::TextDisabled(
                "Confirm phase: %s · DFS open=%d · history only if camera unlocks lookback",
                pname, ui.trace_offset);
        }
        ImGui::TextDisabled("Tx list: click a row to expand gas, inputs, outputs.");
    }
    ImGui::End();
}
