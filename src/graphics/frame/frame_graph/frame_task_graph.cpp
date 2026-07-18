#include "graphics/pch.h"
#include "graphics/frame/frame_graph/frame_task_graph.hpp"

#include <queue>
#include <stdexcept>
#include <sstream>

namespace frame_graph
{

void access_to_barrier(ResourceAccess access, VkImageLayout& layout,
                       VkPipelineStageFlags2& stage, VkAccessFlags2& access_mask,
                       VkImageAspectFlags& aspect)
{
    aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    access_mask = 0;

    switch (access)
    {
    case ResourceAccess::None:
        break;
    case ResourceAccess::ColorAttachment:
        layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        break;
    case ResourceAccess::DepthAttachment:
        layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    case ResourceAccess::DepthSampled:
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    case ResourceAccess::ComputeSampled:
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    case ResourceAccess::ComputeStorageWrite:
        layout = VK_IMAGE_LAYOUT_GENERAL;
        stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        break;
    case ResourceAccess::TransferSrc:
        layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access_mask = VK_ACCESS_2_TRANSFER_READ_BIT;
        break;
    case ResourceAccess::TransferDst:
        layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        break;
    case ResourceAccess::Present:
        layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        access_mask = 0;
        break;
    case ResourceAccess::ShaderRead:
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        access_mask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        break;
    }
}

void FrameTaskGraph::clear()
{
    passes_.clear();
    edges_.clear();
    order_.clear();
    inbound_.clear();
    compiled_ = false;
}

uint32_t FrameTaskGraph::add_pass(PassNode node)
{
    compiled_ = false;
    const uint32_t id = static_cast<uint32_t>(passes_.size());
    passes_.push_back(std::move(node));
    return id;
}

void FrameTaskGraph::add_edge(uint32_t from, uint32_t to, ImageBarrierEdge barrier)
{
    if (from >= passes_.size() || to >= passes_.size())
        throw std::runtime_error("FrameTaskGraph::add_edge: bad pass index");
    compiled_ = false;
    edges_.push_back(Edge{ from, to, barrier });
}

void FrameTaskGraph::compile(const DeviceQueues* queues)
{
    const uint32_t n = static_cast<uint32_t>(passes_.size());
    inbound_.assign(n, {});
    std::vector<uint32_t> indeg(n, 0);
    std::vector<std::vector<uint32_t>> adj(n);

    for (Edge& e : edges_)
    {
        // Fill barrier fields from access pair
        VkImageAspectFlags aspect_from = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageAspectFlags aspect_to = VK_IMAGE_ASPECT_COLOR_BIT;
        access_to_barrier(e.barrier.from_access, e.barrier.old_layout, e.barrier.src_stage,
                          e.barrier.src_access, aspect_from);
        access_to_barrier(e.barrier.to_access, e.barrier.new_layout, e.barrier.dst_stage,
                          e.barrier.dst_access, aspect_to);
        e.barrier.aspect = aspect_to != VK_IMAGE_ASPECT_COLOR_BIT ? aspect_to : aspect_from;

        // Queue family transfer when producer/consumer queues differ
        if (queues)
        {
            const QueueType q_from = passes_[e.from].queue;
            const QueueType q_to = passes_[e.to].queue;
            if (!queues->same_family(q_from, q_to))
            {
                e.barrier.src_queue_family = queues->family_index(q_from);
                e.barrier.dst_queue_family = queues->family_index(q_to);
            }
        }

        adj[e.from].push_back(e.to);
        indeg[e.to]++;
        inbound_[e.to].push_back(e.barrier);
    }

    order_.clear();
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; ++i)
        if (indeg[i] == 0)
            q.push(i);

    while (!q.empty())
    {
        const uint32_t u = q.front();
        q.pop();
        order_.push_back(u);
        for (uint32_t v : adj[u])
        {
            if (--indeg[v] == 0)
                q.push(v);
        }
    }

    if (order_.size() != n)
        throw std::runtime_error("FrameTaskGraph::compile: cycle detected in pass DAG");

    compiled_ = true;
}

const std::vector<ImageBarrierEdge>& FrameTaskGraph::inbound_barriers(uint32_t pass_index) const
{
    if (!compiled_ || pass_index >= inbound_.size())
        throw std::runtime_error("FrameTaskGraph::inbound_barriers: not compiled / bad index");
    return inbound_[pass_index];
}

void FrameTaskGraph::emit_barrier(VkCommandBuffer cmd, VkImage image, const ImageBarrierEdge& e)
{
    if (!cmd || image == VK_NULL_HANDLE)
        return;

    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = e.src_stage ? e.src_stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    b.srcAccessMask = e.src_access;
    b.dstStageMask = e.dst_stage ? e.dst_stage : VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    b.dstAccessMask = e.dst_access;
    b.oldLayout = e.old_layout;
    b.newLayout = e.new_layout;
    b.srcQueueFamilyIndex = e.src_queue_family;
    b.dstQueueFamilyIndex = e.dst_queue_family;
    b.image = image;
    b.subresourceRange = { e.aspect, 0, 1, 0, 1 };

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

std::string FrameTaskGraph::debug_order_string() const
{
    std::ostringstream oss;
    for (size_t i = 0; i < order_.size(); ++i)
    {
        if (i)
            oss << " -> ";
        const auto& p = passes_[order_[i]];
        oss << (p.name ? p.name : "?") << "[" << queue_type_name(p.queue) << "]";
    }
    return oss.str();
}

} // namespace frame_graph
