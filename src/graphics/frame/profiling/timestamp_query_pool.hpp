#pragma once

// Per in-flight-frame Vulkan TIMESTAMP query pools. Used by FrameProfiler.
// Host resolves after the frame fence; ticks → ns via device timestampPeriod.

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class TimestampQueryPool
{
public:
    bool create(VkDevice device,
                float timestamp_period_ns,
                uint32_t frames_in_flight,
                uint32_t queries_per_frame);
    void destroy(VkDevice device);

    bool valid() const { return !pools_.empty(); }
    uint32_t frames_in_flight() const { return frames_; }
    uint32_t queries_per_frame() const { return queries_per_frame_; }
    float timestamp_period_ns() const { return period_ns_; }

    // Record reset for the whole slot (call once at start of first CB for the frame).
    void reset(VkCommandBuffer cmd, uint32_t frame_slot) const;

    void write(VkCommandBuffer cmd,
               uint32_t frame_slot,
               uint32_t query_index,
               VkPipelineStageFlags2 stage) const;

    // After GPU work for this slot has finished (frame fence waited).
    // Only reads the first `used_query_count` queries (unused queries after reset hang WAIT forever).
    // Returns false if pool invalid or results unavailable.
    bool resolve(VkDevice device, uint32_t frame_slot, uint32_t used_query_count);

    // Milliseconds between two query indices (after successful resolve).
    double ms_between(uint32_t frame_slot, uint32_t begin_q, uint32_t end_q) const;

    // Raw tick (0 if unresolved / OOB).
    uint64_t tick(uint32_t frame_slot, uint32_t query_index) const;

private:
    float period_ns_ = 1.f;
    uint32_t frames_ = 0;
    uint32_t queries_per_frame_ = 0;
    std::vector<VkQueryPool> pools_;
    std::vector<std::vector<uint64_t>> results_;
    std::vector<bool> resolved_;
};
