#include "app/pch.h"
#include "app/blockflow_overlay.hpp"
#include "app/ui_chrome.hpp"
#include "network/network_domain.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <glm/glm.hpp>

// Shard colors for feed / inspector labels (UX only â€” not engine)
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

void BlockflowOverlay::set_domain_urls(std::vector<std::string> urls)
{
    domain_urls_ = std::move(urls);
}

void BlockflowOverlay::set_initial_domain(NetworkDomain d)
{
    domain_ = d;
}

std::string BlockflowOverlay::resolve_url_(NetworkDomain d) const
{
    std::vector<const char*> ptrs;
    ptrs.reserve(domain_urls_.size());
    for (const auto& u : domain_urls_)
        ptrs.push_back(u.c_str());
    return network_domain_resolve_url(d, ptrs.empty() ? nullptr : ptrs.data(),
                                      static_cast<int>(ptrs.size()));
}

void BlockflowOverlay::apply_domain_if_changed_()
{
    const int eng = engine_.network_domain();
    if (eng == static_cast<int>(domain_))
        return;
    // Sync from engine if something else switched (should be rare).
    if (domain_ != NetworkDomain::Debug)
        domain_ = static_cast<NetworkDomain>(eng);
}

void BlockflowOverlay::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt_sec = (io.DeltaTime > 0.f) ? io.DeltaTime : (1.f / 60.f);

    const float ui_w = io.DisplaySize.x;
    const float ui_h = io.DisplaySize.y;
    const float rail_w = ui_chrome::rail_width(ui_w);
    const float mx = io.MousePos.x;
    const float my = io.MousePos.y;
    // Full-height center band between Network and Block rails.
    const bool over_scene =
        !io.WantCaptureMouse &&
        mx >= rail_w && my >= 0.f &&
        mx < ui_w - rail_w &&
        my < ui_h;

    // Camera motion â€” Z-track (keys + wheel)
    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))
            camera_.nudge_scroll(CameraController::kEyeZStep * dt_sec);
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
            camera_.nudge_scroll(-CameraController::kEyeZStep * dt_sec);
    }
    // Positive MouseWheel = scroll up â†’ +scroll_z (matches Up arrow).
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
        {
            // Clear selection + home look/pan + reattach camera to live timeline Z.
            engine_.clear_selection();
            camera_.reattach_timeline();
        }
        rmb_down_over_scene_ = false;
        rmb_dragged_ = false;
        rmb_drag_dist_px_ = 0.f;
    }

    const UiSnapshot ui = engine_.copy_ui_snapshot();
    apply_domain_if_changed_();

    draw_network(ui, ui_w, ui_h);
    draw_inspector(ui, ui_w, ui_h);
}

void BlockflowOverlay::draw_network(const UiSnapshot& ui, float ui_w, float ui_h)
{
    const float rail_w = ui_chrome::rail_width(ui_w);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, ui_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Network", nullptr, flags);

    // Domain combo
    ImGui::TextDisabled("Domain");
    const char* labels[] = { "Mainnet", "Testnet", "Debug" };
    int domain_i = static_cast<int>(domain_);
    if (domain_i < 0 || domain_i > 2)
        domain_i = 0;
    if (ImGui::BeginCombo("##domain", labels[domain_i]))
    {
        for (int i = 0; i < 2; ++i) // Mainnet + Testnet only
        {
            const bool selected = (domain_i == i);
            if (ImGui::Selectable(labels[i], selected))
            {
                const NetworkDomain next = static_cast<NetworkDomain>(i);
                if (next != domain_ && !engine_.network_is_switching())
                {
                    const std::string url = resolve_url_(next);
                    if (engine_.switch_network_domain(i, url))
                    {
                        domain_ = next;
                        session_start_ms_ = static_cast<int64_t>(std::time(nullptr)) * 1000;
                        camera_.reattach_timeline();
                    }
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        // Debug: visible but disabled
        ImGui::BeginDisabled(true);
        ImGui::Selectable(labels[2], false);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Simulator planned â€” not available yet");
        ImGui::EndCombo();
    }

    const std::string url_fallback = resolve_url_(domain_);
    const char* url_show = ui.net_base_url[0] ? ui.net_base_url : url_fallback.c_str();
    ImGui::TextDisabled("Endpoint");
    ImGui::TextWrapped("%s", url_show);
    if (ImGui::IsItemHovered() && url_show[0])
        ImGui::SetTooltip("%s", url_show);

    // Status pill
    const NetworkStatus st = static_cast<NetworkStatus>(
        ui.net_switching ? static_cast<int>(NetworkStatus::Switching) : ui.net_status);
    ImVec4 status_col(0.7f, 0.7f, 0.7f, 1.f);
    if (st == NetworkStatus::Steady)
        status_col = ImVec4(0.25f, 0.9f, 0.4f, 1.f);
    else if (st == NetworkStatus::Bootstrapping || st == NetworkStatus::IdentifyTips ||
             st == NetworkStatus::ConfirmWalk || st == NetworkStatus::Connecting)
        status_col = ImVec4(1.f, 0.75f, 0.2f, 1.f);
    else if (st == NetworkStatus::Switching)
        status_col = ImVec4(0.4f, 0.8f, 1.f, 1.f);
    else if (st == NetworkStatus::Error)
        status_col = ImVec4(1.f, 0.3f, 0.25f, 1.f);
    ImGui::Text("Status");
    ImGui::SameLine();
    ImGui::TextColored(status_col, "%s", network_status_label(st));

    ImGui::Separator();
    ImGui::Text("Loading");

    const float win_need = static_cast<float>(std::max(1, ui.lookback_windows_need));
    const float win_frac =
        std::clamp(static_cast<float>(ui.lookback_windows_done) / win_need, 0.f, 1.f);
    ImGui::ProgressBar(win_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("windows %d / %d", ui.lookback_windows_done, ui.lookback_windows_need);

    const float frontier_frac =
        std::clamp(static_cast<float>(ui.lanes_with_frontier) / 16.f, 0.f, 1.f);
    ImGui::ProgressBar(frontier_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("frontier lanes %d / 16", ui.lanes_with_frontier);
    ImGui::TextDisabled("open confirm walks %d", ui.open_confirm_walks);

    float pool_frac = 0.f;
    if (ui.total_blocks > 0)
        pool_frac = std::clamp(static_cast<float>(ui.total_blocks) / 256.f, 0.f, 1.f);
    ImGui::ProgressBar(pool_frac, ImVec2(-1.f, 0.f));
    ImGui::TextDisabled("pool blocks %d", ui.total_blocks);
    ImGui::TextDisabled("fetches admitted %d", ui.stats_fetch_admitted);

    // Timeline segments (lookback windows) with per-segment load %.
    if (ui.segment_count > 0 && ImGui::CollapsingHeader("Segments", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const int nshow =
            std::min(ui.segment_count, UiSnapshot::kMaxTimeSegments);
        for (int i = 0; i < nshow; ++i)
        {
            const auto& s = ui.segments[i];
            const int64_t span_ms = std::max<int64_t>(1, s.to_ms - s.from_ms);
            const float span_s = static_cast<float>(span_ms) * 0.001f;
            const float ratio = std::clamp(s.load_ratio, 0.f, 1.f);
            const char* tag = (i == 0) ? "Live" : (s.confirmed_full ? "full" : "loading");
            ImGui::PushID(i);
            if (i == 0)
                ImGui::Text("[%d] Live  %.0fs", s.index, span_s);
            else
                ImGui::Text("[%d] -%.0fm  %.0fs", s.index, span_s * static_cast<float>(i) / 60.f,
                            span_s);
            ImGui::ProgressBar(ratio, ImVec2(-1.f, 0.f),
                               s.confirmed_full ? "100%" : nullptr);
            ImGui::TextDisabled("  %d blks · %d%% · %s", s.block_count,
                                static_cast<int>(ratio * 100.f + 0.5f), tag);
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::Text("Activity");
    ImGui::TextDisabled("phase: %s", network_status_label(st));
    ImGui::Text("confirmed frontier tips: %d", ui.confirmed_tip_count);
    ImGui::Text("live tips: %d", ui.tip_count);
    ImGui::TextDisabled("is_main API calls: %d", ui.stats_api_is_main);
    ImGui::TextDisabled("removed: %d", ui.stats_removed);
    ImGui::TextDisabled("seed queue: %d", ui.stats_seed_q);

    if (ui.last_poll_ms > 0)
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const float age_s = static_cast<float>(now - ui.last_poll_ms) * 0.001f;
        ImGui::TextDisabled("last poll: %.1fs ago", age_s);
        ImGui::TextDisabled("poll interval: %.0fs", ui.poll_interval_sec);
    }
    else
        ImGui::TextDisabled("last poll: â€”");

    // One shard per line so nothing clips in the narrow rail.
    if (ImGui::TreeNode("Shard tips (H_c / net tip)"))
    {
        for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        {
            for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            {
                const int lane = f * ALPH_NUM_GROUPS + t;
                const int hc = ui.confirmed_height_by_lane[lane];
                const int tip = ui.tip_height_by_lane[lane];
                if (hc < 0 && tip < 0)
                    ImGui::TextDisabled("  %dâ†’%d:  â€” / â€”", f, t);
                else if (hc < 0)
                    ImGui::Text("  %dâ†’%d:  â€” / %d", f, t, tip);
                else if (tip < 0)
                    ImGui::Text("  %dâ†’%d:  %d / â€”", f, t, hc);
                else
                    ImGui::Text("  %dâ†’%d:  %d / %d", f, t, hc, tip);
            }
        }
        ImGui::TreePop();
    }

    // Former bottom "Blockflow" toolbar â€” collapsible under Network.
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Blockflow", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const int64_t now = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const float elapsed_ms = static_cast<float>(now - session_start_ms_) + 1e-3f;
        const float bps = ui.total_blocks / (0.001f * elapsed_ms);

        ImGui::Text("camera z: %.1f", camera_.camera().eye.z);
        ImGui::TextDisabled("  (Up/Down / wheel)");
        ImGui::Text("blocks: %d", ui.total_blocks);
        ImGui::Text("rate: %1.2f/s", bps);
        ImGui::Text("frontier tips: %d", ui.confirmed_tip_count);
        ImGui::Text("live tips: %d", ui.tip_count);

        ImGui::Spacing();
        ImGui::TextDisabled("Confirmed H_c by shard:");
        for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        {
            for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            {
                const int lane = f * ALPH_NUM_GROUPS + t;
                const int hc = ui.confirmed_height_by_lane[lane];
                if (hc < 0)
                    ImGui::TextDisabled("  %dâ†’%d:  â€”", f, t);
                else
                    ImGui::Text("  %dâ†’%d:  %d", f, t, hc);
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Recent feed");
        ImGui::BeginChild("feed", ImVec2(0, 180.f), true, ImGuiWindowFlags_HorizontalScrollbar);
        int feed_i = 0;
        for (const FeedEntry& entry : ui.feed)
        {
            ImGui::PushID(feed_i++);
            ImGui::PushID(entry.hash.c_str());
            const int shardId = entry.chain_idx() & 15;
            ImGui::TextColored(
                ImVec4(kShardColors[shardId].r, kShardColors[shardId].g,
                       kShardColors[shardId].b, 1.0f),
                "[%dâ†’%d]", entry.chainFrom, entry.chainTo);
            ImGui::SameLine();
            // Shorten hash label in narrow rail; full hash on hover / clipboard.
            char short_h[20];
            if (entry.hash.size() > 12)
                std::snprintf(short_h, sizeof(short_h), "%.6sâ€¦%.4s", entry.hash.c_str(),
                              entry.hash.c_str() + entry.hash.size() - 4);
            else
                std::snprintf(short_h, sizeof(short_h), "%s", entry.hash.c_str());
            if (ImGui::SmallButton(short_h))
            {
                engine_.set_selection(entry.hash);
                ImGui::SetClipboardText(entry.hash.c_str());
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n(click select + copy)", entry.hash.c_str());
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

namespace
{
// Compact hash for tree labels (full id still available via link when expanded).
void short_id(const std::string& s, char* buf, size_t buf_n)
{
    if (s.empty())
    {
        snprintf(buf, buf_n, "â€”");
        return;
    }
    if (s.size() <= 14)
    {
        snprintf(buf, buf_n, "%s", s.c_str());
        return;
    }
    snprintf(buf, buf_n, "%.6sâ€¦%.4s", s.c_str(), s.c_str() + s.size() - 4);
}

// Host set by draw_inspector from current domain (mainnet/testnet explorer).
const char* g_explorer_host = "https://explorer.alephium.org";

void explorer_url(char* buf, size_t n, const char* kind, const std::string& id)
{
    snprintf(buf, n, "%s/%s/%s", g_explorer_host, kind, id.c_str());
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
    const float rail_w = ui_chrome::rail_width(ui_w);
    g_explorer_host = network_domain_explorer_host(domain_);

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
    ImGui::SetNextWindowPos(ImVec2(ui_w - rail_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, ui_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("Block", nullptr, flags);

    char url[512];
    char id_buf[64];
    if (!inspector.hash.empty())
    {
        explorer_url(url, sizeof(url), "blocks", inspector.hash);
        short_id(inspector.hash, id_buf, sizeof(id_buf));

        // â”€â”€ Block header (always open, compact) â”€â”€
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
            "chain [%dâ†’%d]", inspector.chainFrom, inspector.chainTo);

        if (inspector.timestamp > 0)
        {
            const time_t t = static_cast<time_t>(inspector.timestamp / 1000);
            char tbuf[40];
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            ImGui::TextDisabled("time %s", tbuf);
        }

        ImGui::TextDisabled("%d tx Â· %d deps Â· %d uncles",
                            static_cast<int>(inspector.txns.size()),
                            static_cast<int>(inspector.deps.size()),
                            static_cast<int>(inspector.uncles.size()));

        // â”€â”€ Dependencies (collapsed by default) â”€â”€
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

        // â”€â”€ Transactions: one-line headers, expand for detail â”€â”€
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
                "%s  in %d Â· out %d Â· gas %d",
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
            "Camera: wheel/arrows Z (detaches timeline) Â· LMB look Â· short LMB pick Â· RMB pan Â· short RMB reattach live tip");
        ImGui::TextDisabled(
            "Solid=main+deps Â· green=frontier tip + blockDeps Â· cyan=unconfirmed children of frontier Â· orange=missing deps Â· gold=select");
        {
            // User-facing names (adapter phases still Bootstrap/IdentifyTips/DfsTrace/Steady).
            const char* pname = "Bootstrap";
            if (ui.trace_phase == 1) pname = "Identify tips";
            else if (ui.trace_phase == 2) pname = "Confirm walk";
            else if (ui.trace_phase == 3) pname = "Steady";
            ImGui::TextDisabled(
                "Confirm phase: %s Â· open lanes=%d Â· history only if camera unlocks lookback",
                pname, ui.trace_offset);
        }
        ImGui::TextDisabled("Tx list: click a row to expand gas, inputs, outputs.");
    }
    ImGui::End();
}
