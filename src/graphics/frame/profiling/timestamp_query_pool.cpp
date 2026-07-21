#include "graphics/pch.h"
#include "graphics/frame/profiling/timestamp_query_pool.hpp"

#include <cstdio>

bool TimestampQueryPool::create(VkDevice device,
                                float timestamp_period_ns,
                                uint32_t frames_in_flight,
                                uint32_t queries_per_frame)
{
    destroy(device);
    if (!device || frames_in_flight == 0 || queries_per_frame == 0)
        return false;

    period_ns_ = timestamp_period_ns > 0.f ? timestamp_period_ns : 1.f;
    frames_ = frames_in_flight;
    queries_per_frame_ = queries_per_frame;
    pools_.assign(frames_, VK_NULL_HANDLE);
    results_.assign(frames_, std::vector<uint64_t>(queries_per_frame_, 0));
    resolved_.assign(frames_, false);

    for (uint32_t i = 0; i < frames_; ++i)
    {
        VkQueryPoolCreateInfo ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        ci.queryCount = queries_per_frame_;
        if (vkCreateQueryPool(device, &ci, nullptr, &pools_[i]) != VK_SUCCESS)
        {
            std::printf("[gfx] TimestampQueryPool: create failed for slot %u\n", i);
            destroy(device);
            return false;
        }
    }
    return true;
}

void TimestampQueryPool::destroy(VkDevice device)
{
    if (device)
    {
        for (VkQueryPool& p : pools_)
        {
            if (p != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(device, p, nullptr);
                p = VK_NULL_HANDLE;
            }
        }
    }
    pools_.clear();
    results_.clear();
    resolved_.clear();
    frames_ = 0;
    queries_per_frame_ = 0;
}

void TimestampQueryPool::reset(VkCommandBuffer cmd, uint32_t frame_slot) const
{
    if (!cmd || frame_slot >= pools_.size() || pools_[frame_slot] == VK_NULL_HANDLE)
        return;
    vkCmdResetQueryPool(cmd, pools_[frame_slot], 0, queries_per_frame_);
}

void TimestampQueryPool::write(VkCommandBuffer cmd,
                               uint32_t frame_slot,
                               uint32_t query_index,
                               VkPipelineStageFlags2 stage) const
{
    if (!cmd || frame_slot >= pools_.size() || pools_[frame_slot] == VK_NULL_HANDLE)
        return;
    if (query_index >= queries_per_frame_)
        return;

    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    // Timestamps do not need a memory barrier; writeTimestamp2 alone is enough.
    (void)mb;

    vkCmdWriteTimestamp2(cmd, stage, pools_[frame_slot], query_index);
}

bool TimestampQueryPool::resolve(VkDevice device, uint32_t frame_slot, uint32_t used_query_count)
{
    if (!device || frame_slot >= pools_.size() || pools_[frame_slot] == VK_NULL_HANDLE)
        return false;
    if (used_query_count == 0)
    {
        resolved_[frame_slot] = false;
        return false;
    }
    if (used_query_count > queries_per_frame_)
        used_query_count = queries_per_frame_;

    resolved_[frame_slot] = false;
    auto& out = results_[frame_slot];
    out.assign(queries_per_frame_, 0);

    // Only fetch written queries — unused slots after reset stay unavailable and hang WAIT.
    const VkResult r = vkGetQueryPoolResults(
        device,
        pools_[frame_slot],
        0,
        used_query_count,
        sizeof(uint64_t) * used_query_count,
        out.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    if (r != VK_SUCCESS && r != VK_NOT_READY)
    {
        std::printf("[gfx] TimestampQueryPool: get results failed (%d)\n", static_cast<int>(r));
        return false;
    }
    if (r == VK_NOT_READY)
        return false;

    resolved_[frame_slot] = true;
    return true;
}

double TimestampQueryPool::ms_between(uint32_t frame_slot, uint32_t begin_q, uint32_t end_q) const
{
    if (frame_slot >= results_.size() || !resolved_[frame_slot])
        return 0.0;
    if (begin_q >= queries_per_frame_ || end_q >= queries_per_frame_)
        return 0.0;

    const uint64_t t0 = results_[frame_slot][begin_q];
    const uint64_t t1 = results_[frame_slot][end_q];
    if (t1 < t0)
        return 0.0;

    const double ns = static_cast<double>(t1 - t0) * static_cast<double>(period_ns_);
    return ns * 1e-6;
}

uint64_t TimestampQueryPool::tick(uint32_t frame_slot, uint32_t query_index) const
{
    if (frame_slot >= results_.size() || !resolved_[frame_slot])
        return 0;
    if (query_index >= results_[frame_slot].size())
        return 0;
    return results_[frame_slot][query_index];
}
