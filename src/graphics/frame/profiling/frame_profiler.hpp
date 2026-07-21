#pragma once

// CPU scopes + GPU timestamp pairs per named pass (FrameTaskGraph names + CPU prep).
// Resolve GPU after the in-flight frame fence; expose rolling snapshot for HUD / vnv bench.

#include "graphics/frame/profiling/timestamp_query_pool.hpp"
#include "graphics/gpu_pub_lib.h"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class FrameProfiler
{
public:
    static constexpr uint32_t kQueriesPerFrame = 64; // 32 begin/end pairs
    static constexpr uint32_t kRingFrames      = 120;

    bool create(VkDevice device, float timestamp_period_ns, uint32_t frames_in_flight);
    void destroy(VkDevice device);

    void set_enabled(bool on);
    bool enabled() const { return enabled_; }

    // Call after wait_frame for this slot: resolve prior GPU marks, then prepare recording.
    void on_frame_slot_ready(VkDevice device, uint32_t frame_slot);

    // Start of recording into frame_slot (after on_frame_slot_ready).
    void begin_record(uint32_t frame_slot);

    // First command buffer for the slot must reset the query pool once.
    void ensure_pool_reset(VkCommandBuffer cmd);

    // Wall-clock end of host frame work (ms). Builds latest snapshot (CPU + last GPU).
    void end_host_frame(float frame_wall_ms);

    // --- CPU scopes (render thread) ---
    class CpuScope
    {
    public:
        CpuScope(FrameProfiler* p, const char* name);
        ~CpuScope();
        CpuScope(const CpuScope&) = delete;
        CpuScope& operator=(const CpuScope&) = delete;
        CpuScope(CpuScope&& o) noexcept;
        CpuScope& operator=(CpuScope&&) = delete;

    private:
        FrameProfiler* p_ = nullptr;
        const char* name_ = nullptr;
        std::chrono::steady_clock::time_point t0_{};
    };

    CpuScope cpu_scope(const char* name) { return CpuScope(this, name); }

    // --- GPU scopes (command buffer recording) ---
    class GpuScope
    {
    public:
        GpuScope(FrameProfiler* p,
                 VkCommandBuffer cmd,
                 const char* name,
                 VkPipelineStageFlags2 begin_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                 VkPipelineStageFlags2 end_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
        ~GpuScope();
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;
        GpuScope(GpuScope&& o) noexcept;
        GpuScope& operator=(GpuScope&&) = delete;

    private:
        FrameProfiler* p_ = nullptr;
        VkCommandBuffer cmd_ = VK_NULL_HANDLE;
        const char* name_ = nullptr;
        VkPipelineStageFlags2 end_stage_ = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        uint32_t begin_q_ = UINT32_MAX;
        bool active_ = false;
    };

    GpuScope gpu_scope(VkCommandBuffer cmd,
                       const char* name,
                       VkPipelineStageFlags2 begin_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                       VkPipelineStageFlags2 end_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)
    {
        return GpuScope(this, cmd, name, begin_stage, end_stage);
    }

    // Manual GPU begin/end (multi-CB paths that cannot use RAII across submit).
    uint32_t gpu_begin(VkCommandBuffer cmd,
                       const char* name,
                       VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    void gpu_end(VkCommandBuffer cmd,
                 uint32_t begin_query,
                 const char* name,
                 VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    void add_cpu_ms(const char* name, float ms);

    void copy_snapshot(FrameTimingSnapshot& out) const;
    FrameTimingSnapshot snapshot() const;

    TimestampQueryPool& pool() { return pool_; }
    const TimestampQueryPool& pool() const { return pool_; }

private:
    struct GpuMark
    {
        const char* name = nullptr;
        uint32_t begin_q = 0;
        uint32_t end_q = 0;
        bool complete = false;
    };

    struct ScopeAccum
    {
        double cpu_sum = 0;
        double gpu_sum = 0;
        uint32_t samples = 0;
        // Ring for percentile (store last kRingFrames samples as packed floats)
        std::vector<float> cpu_ring;
        std::vector<float> gpu_ring;
        uint32_t ring_i = 0;
        uint32_t ring_n = 0;
    };

    void record_scope_sample_(const char* name, float cpu_ms, float gpu_ms);
    void rebuild_snapshot_locked_();
    static float percentile_ring_(const std::vector<float>& ring, uint32_t n, float p);

    mutable std::mutex mu_;
    bool enabled_ = false;
    bool created_ = false;

    TimestampQueryPool pool_;
    uint32_t frames_in_flight_ = 0;
    uint32_t record_slot_ = 0;
    bool pool_reset_recorded_ = false;
    uint32_t next_query_ = 0;

    std::vector<std::vector<GpuMark>> gpu_marks_; // per frame slot
    std::vector<uint32_t> used_queries_;          // per frame slot (for resolve range)
    // CPU ms this host frame (name → ms), flushed in end_host_frame
    std::unordered_map<std::string, float> cpu_this_frame_;
    // Last resolved GPU ms by name
    std::unordered_map<std::string, float> last_gpu_ms_;

    std::unordered_map<std::string, ScopeAccum> accum_;
    FrameTimingSnapshot snapshot_{};
    float last_frame_ms_ = 0.f;
    uint64_t sample_index_ = 0;
    bool had_prior_gpu_resolve_ = false;
};
