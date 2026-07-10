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

BlockflowOverlay::BlockflowOverlay(CameraState& camera, IBlockvizEngine& engine)
    : camera_(camera)
    , engine_(engine)
{
    session_start_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
}

void BlockflowOverlay::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt_sec = (io.DeltaTime > 0.f) ? io.DeltaTime : (1.f / 60.f);

    // Camera keys (Up/Down) — client-owned CameraState
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
            camera_.nudge_eye_z(CameraState::kEyeZStep * dt_sec);
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
            camera_.nudge_eye_z(-CameraState::kEyeZStep * dt_sec);
    }

    const UiSnapshot ui = engine_.copy_ui_snapshot();
    const float ui_w = io.DisplaySize.x;
    const float ui_h = io.DisplaySize.y;

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

    const CameraState::Snapshot cam = camera_.snapshot();
    float mps = cam.meters_per_second;

    const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
    const float elapsed_ms = static_cast<float>(now - session_start_ms_) + 1e-3f;
    const float bps = ui.total_blocks / (0.001f * elapsed_ms);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::SliderFloat("Scroll speed", &mps, CameraState::kMpsMin, CameraState::kMpsMax, "%.1f"))
        camera_.set_meters_per_second(mps);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera auto-scroll rate along the chain axis");

    ImGui::Text("z: %.1f  (Up/Down)", cam.eye_z);
    ImGui::SameLine(0.f, 24.f);
    ImGui::Text("blocks: %d", ui.total_blocks);
    ImGui::SameLine(0.f, 24.f);
    ImGui::Text("rate: %1.2f/s", bps);

    ImGui::Separator();
    ImGui::BeginChild("feed", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const FeedEntry& entry : ui.feed)
    {
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
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

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
    if (!inspector.hash.empty())
    {
        memset(url, 0, sizeof(url));
        snprintf(url, sizeof(url), "https://explorer.alephium.org/blocks/%s",
                 inspector.hash.c_str());

        ImGui::Text("hash:");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL(inspector.hash.c_str(), url);

        ImGui::Text("height: %d", inspector.height);

        const int shardId = inspector.chain_idx() & 15;
        ImGui::Text("chain:");
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(kShardColors[shardId].r, kShardColors[shardId].g, kShardColors[shardId].b, 1.0f),
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
