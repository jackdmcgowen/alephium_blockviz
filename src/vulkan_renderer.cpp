#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <cjson/cJSON.h>
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


static uint32_t lastPickedID = ~0u;
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

static const uint32_t statusBarHeight = 200;

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
    , chains(16)
{
}   /* VulkanRenderer() */

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


void VulkanRenderer::Add_Block(cJSON* block)
{
    AlphBlock alph_block(block);

    std::lock_guard<std::mutex> lock(dataMutex);


    uint8_t chainIndex = alph_block.chain_idx();

    auto& heightMap = chains[chainIndex];
    auto& blocksAtHeight = heightMap[alph_block.height];

    auto result = blocksAtHeight.emplace(alph_block.hash, alph_block);
    if (result.second)
        blockQueue.push_back(alph_block);
    else
        printf("duplicate\n");

    // Collect uncle removals for dual-write (same scan as chains eviction)
    std::vector<NodeId> removed_uncles;
    for (auto& bh : heightMap)
    {
        for (auto unc : alph_block.uncles)
        {
            auto uncle_find = bh.second.find(unc);
            if (uncle_find != bh.second.end())
            {
                removed_uncles.push_back(unc);
                bh.second.erase(unc);
            }
        }
    }

    // Dual-write: BlockGraph (slim node) + AlphDetailStore (full block for inspector)
    {
        GraphDelta delta;
        if (result.second)
        {
            GraphNode node;
            node.id = alph_block.hash;
            node.timestamp_ms = alph_block.timestamp;
            node.height = alph_block.height;
            node.group_from = alph_block.chainFrom;
            node.group_to = alph_block.chainTo;
            node.lane = alph_block.chain_idx();
            node.lane_count_hint = 16;
            node.chain_label = std::to_string(alph_block.chainFrom) + "->" + std::to_string(alph_block.chainTo);
            delta.upsert_nodes.push_back(std::move(node));
            // edges optional v1 — leave empty until edge viz (OQ3)
            detail_store_.upsert(alph_block);
        }
        if (!removed_uncles.empty())
        {
            delta.remove_nodes = removed_uncles;
            detail_store_.remove_many(removed_uncles);
        }
        if (!delta.upsert_nodes.empty() || !delta.remove_nodes.empty())
            block_graph_.apply(delta);
    }

    if (dual_write_validate_)
    {
        std::vector<NodeId> chains_ids;
        chains_ids.reserve(4096);
        for (const auto& hm : chains)
        {
            for (const auto& height_entry : hm)
            {
                for (const auto& hash_entry : height_entry.second)
                    chains_ids.push_back(hash_entry.first);
            }
        }
        std::sort(chains_ids.begin(), chains_ids.end());
        chains_ids.erase(std::unique(chains_ids.begin(), chains_ids.end()), chains_ids.end());
        const std::vector<NodeId> graph_ids = block_graph_.live_ids_sorted();
        if (chains_ids != graph_ids)
        {
            printf("[dual-write] hash-set mismatch: chains=%zu graph=%zu\n",
                   chains_ids.size(), graph_ids.size());
        }
    }

    total_blocks++;

    if (blockQueue.size() > 120)
        blockQueue.pop_back();

    dataCond.notify_one();

}   /* Add_Block() */


void VulkanRenderer::Remove_Block(const std::string& hash)
{
    if (hash.empty())
        return;

    std::lock_guard<std::mutex> lock(dataMutex);

    bool erased = false;
    for (auto& heightMap : chains)
    {
        for (auto hit = heightMap.begin(); hit != heightMap.end(); )
        {
            auto& blocks = hit->second;
            auto fit = blocks.find(hash);
            if (fit != blocks.end())
            {
                blocks.erase(fit);
                erased = true;
            }
            if (blocks.empty())
                hit = heightMap.erase(hit);
            else
                ++hit;
        }
    }

    if (!erased)
        return;

    GraphDelta delta;
    delta.remove_nodes.push_back(hash);
    block_graph_.apply(delta);
    detail_store_.remove(hash);

    blockQueue.erase(
        std::remove_if(blockQueue.begin(), blockQueue.end(),
                       [&](const AlphBlock& b) { return b.hash == hash; }),
        blockQueue.end());

    if (selected_block.hash == hash)
        selected_block = AlphBlock{};

    dataCond.notify_one();

}   /* Remove_Block() */


void VulkanRenderer::Start()
{
    running = true;
    renderThread = std::thread(&VulkanRenderer::render_loop, this);

}   /* Start() */


void VulkanRenderer::Stop()
{
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        running = false;
    }
    dataCond.notify_one();
    if (renderThread.joinable())
    {
        renderThread.join();
    }

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
        std::unique_lock<std::mutex> lock(dataMutex);

        instanceCount = 0;
        debugDrawer.clear();

        LayoutParams layout_params;
        layout_params.meters_per_height = meters_per_height;
        layout_params.base_radius = 20.0f;
        layout_params.lane_count = 16;

        LayoutResult layout = polar_layout_.build(chains, layout_params);
        const auto& block_positions = layout.positions;
        const auto& block_shards = layout.lanes;

        for (const PlacedBlock& placed : layout.placements)
        {
            InstanceData inst = { placed.pos, placed.color };

            if (lastPickedID == instanceCount && placed.block)
                selected_block = *placed.block;

            if (instanceCount < MAX_INSTANCES)
            {
                memcpy(static_cast<uint8_t*>(mappedInstanceMemory) + instanceCount * sizeof(InstanceData),
                       &inst, sizeof(InstanceData));
                instanceCount++;
            }
            else
            {
                printf("Instance buffer full\n");
                break;
            }
        }

        // Dependency arrows: only the frontier (max-height tip blocks) per chain — fresh & decluttered.
        // Smaller heads; endpoints inset so cones don't sit inside cube geometry.
        {
            const float tip_len    = std::max(0.18f, meters_per_height * 0.08f);
            const float tip_rad    = std::max(0.06f, meters_per_height * 0.03f);
            const float shaft_r    = tip_rad * 0.4f;
            const float clearance  = std::max(0.55f, meters_per_height * 0.12f);
            constexpr uint32_t kDepRadial    = 8;
            constexpr uint32_t kMaxDepArrows = 512;
            uint32_t arrow_count = 0;

            for (uint8_t chain_from = 0; chain_from < ALPH_NUM_GROUPS; ++chain_from)
            {
                for (uint8_t chain_to = 0; chain_to < ALPH_NUM_GROUPS; ++chain_to)
                {
                    const uint8_t shard_id = static_cast<uint8_t>(chain_from * ALPH_NUM_GROUPS + chain_to);
                    if (shard_id >= chains.size())
                        continue;

                    const HeightToHash& heightMap = chains[shard_id];
                    if (heightMap.empty())
                        continue;

                    // map is ordered by height; last key is the tip height for this chain
                    const auto tip_it = std::prev(heightMap.end());
                    const HashToBlocks& tips_at_height = tip_it->second;

                    const glm::vec3& dest_c = SHARD_COLORS[shard_id % 16];

                    for (const auto& hash_and_block : tips_at_height)
                    {
                        if (arrow_count >= kMaxDepArrows)
                            break;

                        const AlphBlock& block = hash_and_block.second;
                        auto to_it = block_positions.find(block.hash);
                        if (to_it == block_positions.end())
                            continue;

                        const glm::vec3& to_pos = to_it->second;

                        for (const std::string& dep_hash : block.deps)
                        {
                            if (arrow_count >= kMaxDepArrows)
                                break;

                            auto from_it = block_positions.find(dep_hash);
                            if (from_it == block_positions.end())
                                continue;

                            glm::vec3 from_inset, to_inset;
                            if (!inset_segment(from_it->second, to_pos, clearance, from_inset, to_inset))
                                continue;

                            auto from_shard_it = block_shards.find(dep_hash);
                            const bool cross_chain =
                                (from_shard_it != block_shards.end() && from_shard_it->second != shard_id);

                            const float alpha = cross_chain ? 0.90f : 0.55f;
                            const glm::vec4 color(dest_c.r, dest_c.g, dest_c.b, alpha);

                            debugDrawer.add_arrow(from_inset, to_inset, color, tip_len, tip_rad, shaft_r, kDepRadial);
                            ++arrow_count;
                        }
                    }
                }
            }
        }

        //blockQueue.em

        //if( blockQueue.size() > 120)
        //{
        //    blockQueue.pop_back();
        //}

        lock.unlock();

        static AlphBlock selected;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowPos(ImVec2(0, height - statusBarHeight));
        ImGui::SetNextWindowSize(ImVec2(width, statusBarHeight));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("Blockflow", 0, flags);
        {
            int64_t now = time(NULL) * 1000;
            ImGui::SliderFloat("meters/s", &meters_per_second, 1.0f, 50.0f);
            ImGui::SliderFloat("pos", &eye_z, -1000.f, 1000.0f);
            float bps = total_blocks / (0.001f * (now - start));
            ImGui::Text("total %d", total_blocks);
            ImGui::SameLine();
            ImGui::Text("bps %1.2f", bps);
            for (auto &block : blockQueue)
            {

                ImGui::PushID(block.hash.c_str());

                int shardId = block.chain_idx();

                ImGui::TextColored(ImVec4(SHARD_COLORS[shardId].r, SHARD_COLORS[shardId].g, SHARD_COLORS[shardId].b, 1.0f), "[%d->%d]", block.chainFrom, block.chainTo);
                ImGui::SameLine();
                if (ImGui::Button(block.hash.c_str()))
                {
                    selected.hash = block.hash;
                    selected.chainFrom = block.chainFrom;
                    selected.chainTo = block.chainTo;
                    selected.height = block.height;
                    selected.deps.resize(block.deps.size());
                    selected.txns.resize(block.txns.size());
                    std::copy(block.deps.begin(), block.deps.end(), selected.deps.begin());
                    std::copy(block.txns.begin(), block.txns.end(), selected.txns.begin());

                    ImGui::SetClipboardText(block.hash.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, height - statusBarHeight));
        ImGui::SetNextWindowBgAlpha(0.0f);

        flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysHorizontalScrollbar;

        char url[512];
        ImGui::Begin("Block", 0, flags);
        {
            if (selected_block.txns.size())
            {
                memset(url, 0, sizeof(url));
                snprintf(url, 512, "https://explorer.alephium.org/blocks/%s", selected_block.hash.c_str());

                ImGui::TextColored(ImVec4(0, 0, 0, 1), "hash: ");
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(selected_block.hash.c_str(), url);

                ImGui::TextColored(ImVec4(0, 0, 0, 1), "height: %d", selected_block.height);

                int shardId = selected_block.chain_idx();
                ImGui::TextColored(ImVec4(0, 0, 0, 1), "chain: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(SHARD_COLORS[shardId].r, SHARD_COLORS[shardId].g, SHARD_COLORS[shardId].b, 1.0f), "[%d->%d]", selected_block.chainFrom, selected_block.chainTo);

                ImGui::Indent();
                for (auto tx : selected_block.txns)
                {
                    memset(url, 0, sizeof(url));
                    snprintf(url, 512, "https://explorer.alephium.org/transactions/%s", tx.txid.c_str());

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "txid: ");
                    ImGui::SameLine();
                    ImGui::TextLinkOpenURL(tx.txid.c_str(), url);

                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "version: %d", tx.version);
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "networkId: %d", tx.networkId);
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "scriptOpt: %s", tx.scriptOpt.c_str());
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "gasAmount: %d", tx.gasAmount);
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "gasPrice: %s", tx.gasPrice.c_str());

                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "inputs: %d", tx.inputs.size());
                    ImGui::TextColored(ImVec4(0, 0, 0, 1), "outputs: %d", tx.outputs.size());

                    ImGui::Indent();
                    for (auto out : tx.outputs)
                    {
                        memset(url, 0, sizeof(url));
                        snprintf(url, 512, "https://explorer.alephium.org/addresses/%s", out.address.c_str());

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0, 0, 0, 1), "address: ");
                        ImGui::SameLine();
                        ImGui::TextLinkOpenURL(out.address.c_str(), url);


                        ImGui::TextColored(ImVec4(0, 0, 0, 1), "amount: %s", out.toAmount().c_str() );
                    }
                    ImGui::Unindent();
                }
                ImGui::Unindent();
            }
        }
        ImGui::End();
        ImGui::Render();
        update_uniform_buffer();
        render();

        elapsedSeconds += static_cast<float>(dt) * 0.001f; // Scroll speed

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
        inFlightFrames[currentFrame].pendingPick = false;

        uint32_t picked = read_picker_obj_id(device);

        if (picked != INVALID_ID)  // ~0u or 0xFFFFFFFFu
        {
            lastPickedID = picked; //printf("Picked instance/object ID: %u\n", picked);
        }
        else
        {
            lastPickedID = ~0u; //printf("Nothing picked (background)\n");
        }
    }

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


void VulkanRenderer::update_uniform_buffer()
{
    UniformBufferObject ubo{};

    float meters =  meters_per_second * elapsedSeconds;

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, eye_z - meters);
    glm::vec3 center = glm::vec3(0.0f, 0.0f, eye_z - meters + 1);

    std::lock_guard<std::mutex> lk(renderMutex);

    ubo.view = glm::lookAt(eye, center, glm::vec3(0.0f, -1.0f, 0.0f));
    ubo.proj = glm::perspective( FOV, (float)width / height, NEAR_PLANE, FAR_PLANE );

    viewProj = ubo.proj * ubo.view;

    ubo.viewPos = eye;
    ubo.lightPos = center;
    ubo.meters = meters_per_second;

    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniformBufferMemory);

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

        if (io.WantCaptureMouse && io.MouseClicked[ImGuiMouseButton_Left])
        {
            // Mouse click happened outside ImGui UI -> trigger pick
            pickMouseX = static_cast<uint32_t>(io.MousePos.x);
            pickMouseY = static_cast<uint32_t>(io.MousePos.y);

            record_picker_pass(buffer, pickMouseX, pickMouseY);

            inFlightFrames[currentFrame].pendingPick = true;  // flag to read back after submit
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
