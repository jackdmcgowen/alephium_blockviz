#pragma once

// Lightweight GPU pass DAG: topology + image barriers on edges (G2).
// Passes record into caller-provided command buffers; multi-queue submits stay in the executor.
#include "graphics/queue_types.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace frame_graph
{

enum class ResourceAccess : uint8_t
{
    None = 0,
    ColorAttachment,
    DepthAttachment,
    DepthSampled,
    ComputeSampled,
    ComputeStorageWrite,
    TransferSrc,
    TransferDst,
    Present,
    ShaderRead,
};

// Logical resource ids used when building the frame topology.
enum class ResourceId : uint16_t
{
    SwapchainColor = 0,
    SceneDepth,
    SelectionDepth,
    SobelEdges,
    PickerColor,
    Count
};

struct ImageBarrierEdge
{
    ResourceId resource = ResourceId::SwapchainColor;
    ResourceAccess from_access = ResourceAccess::None;
    ResourceAccess to_access = ResourceAccess::None;
    // Filled by compile() from access pair (+ optional queue family transfer).
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 src_stage = 0;
    VkPipelineStageFlags2 dst_stage = 0;
    VkAccessFlags2 src_access = 0;
    VkAccessFlags2 dst_access = 0;
    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct PassContext
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t frame_index = 0;
    uint32_t image_index = 0;
};

using PassRecordFn = std::function<void(const PassContext&)>;

struct PassNode
{
    const char* name = "";
    QueueType queue = QueueType::_3D;
    PassRecordFn record;
    // Optional: resources this pass writes/reads (for docs / validation)
    std::vector<ResourceId> writes;
    std::vector<ResourceId> reads;
};

// Derived barrier stages/layouts from a resource access.
void access_to_barrier(ResourceAccess access, VkImageLayout& layout,
                       VkPipelineStageFlags2& stage, VkAccessFlags2& access_mask,
                       VkImageAspectFlags& aspect);

class IPass;

class FrameTaskGraph
{
public:
    void clear();

    // Returns pass index.
    uint32_t add_pass(PassNode node);

    // Register an IPass node: fills name, queue, and declare_resources reads/writes.
    // Record callback is optional (multi-queue executor may record elsewhere).
    uint32_t register_pass(IPass& pass, PassRecordFn record = {});

    // Dependency: `to` runs after `from`. Barrier describes transition for `resource`.
    void add_edge(uint32_t from, uint32_t to, ImageBarrierEdge barrier);

    // Topo sort + fill barrier stages from access pairs. Throws on cycle.
    void compile(const DeviceQueues* queues = nullptr);

    const std::vector<uint32_t>& order() const { return order_; }
    const std::vector<PassNode>& passes() const { return passes_; }

    // Barriers that must run before pass `pass_index` (inbound edges).
    const std::vector<ImageBarrierEdge>& inbound_barriers(uint32_t pass_index) const;

    // Emit a VkImageMemoryBarrier2 for an edge (image handle supplied by caller).
    static void emit_barrier(VkCommandBuffer cmd, VkImage image, const ImageBarrierEdge& e);

    // Debug: pass names in execution order.
    std::string debug_order_string() const;

private:
    std::vector<PassNode> passes_;
    struct Edge
    {
        uint32_t from = 0;
        uint32_t to = 0;
        ImageBarrierEdge barrier{};
    };
    std::vector<Edge> edges_;
    std::vector<uint32_t> order_;
    std::vector<std::vector<ImageBarrierEdge>> inbound_;
    bool compiled_ = false;
};

} // namespace frame_graph
