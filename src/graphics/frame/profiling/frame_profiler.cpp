#include "graphics/pch.h"
#include "graphics/frame/profiling/frame_profiler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
void copy_name(char* dst, size_t dst_n, const char* src)
{
    if (!dst || dst_n == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dst_n, "%s", src);
}

FrameBoundClass classify_bound(float frame_ms, float cpu_ms, float gpu_ms)
{
    if (frame_ms <= 0.f && cpu_ms <= 0.f && gpu_ms <= 0.f)
        return FrameBoundClass::Unknown;

    const float work = (std::max)(cpu_ms, gpu_ms);
    if (frame_ms > work * 1.35f && frame_ms > work + 0.5f)
        return FrameBoundClass::PresentSync;
    if (cpu_ms > gpu_ms * 1.1f)
        return FrameBoundClass::Cpu;
    if (gpu_ms > cpu_ms * 1.1f)
        return FrameBoundClass::Gpu;
    // Close: pick the larger
    return (cpu_ms >= gpu_ms) ? FrameBoundClass::Cpu : FrameBoundClass::Gpu;
}
} // namespace

bool FrameProfiler::create(VkDevice device, float timestamp_period_ns, uint32_t frames_in_flight)
{
    destroy(device);
    if (!device || frames_in_flight == 0)
        return false;

    if (!pool_.create(device, timestamp_period_ns, frames_in_flight, kQueriesPerFrame))
        return false;

    frames_in_flight_ = frames_in_flight;
    gpu_marks_.assign(frames_in_flight_, {});
    used_queries_.assign(frames_in_flight_, 0);
    created_ = true;
    snapshot_ = {};
    return true;
}

void FrameProfiler::destroy(VkDevice device)
{
    pool_.destroy(device);
    gpu_marks_.clear();
    used_queries_.clear();
    frames_in_flight_ = 0;
    created_ = false;
    enabled_ = false;
    next_query_ = 0;
    pool_reset_recorded_ = false;
    cpu_this_frame_.clear();
    last_gpu_ms_.clear();
    accum_.clear();
    snapshot_ = {};
}

void FrameProfiler::set_enabled(bool on)
{
    enabled_ = on && created_;
}

void FrameProfiler::on_frame_slot_ready(VkDevice device, uint32_t frame_slot)
{
    if (!created_ || !enabled_ || !device || frame_slot >= frames_in_flight_)
        return;

    // Resolve GPU marks from the previous use of this slot (fence already waited).
    const uint32_t used = (frame_slot < used_queries_.size()) ? used_queries_[frame_slot] : 0;
    if (used > 0 && pool_.resolve(device, frame_slot, used))
    {
        last_gpu_ms_.clear();
        for (const GpuMark& m : gpu_marks_[frame_slot])
        {
            if (!m.complete || !m.name)
                continue;
            const float ms = static_cast<float>(pool_.ms_between(frame_slot, m.begin_q, m.end_q));
            last_gpu_ms_[m.name] = ms;
        }
        had_prior_gpu_resolve_ = true;
    }
}

void FrameProfiler::begin_record(uint32_t frame_slot)
{
    if (!created_ || !enabled_)
        return;
    if (frame_slot >= frames_in_flight_)
        return;

    // GPU query bookkeeping only — CPU scopes may already be open (Prepare before render).
    record_slot_ = frame_slot;
    next_query_ = 0;
    pool_reset_recorded_ = false;
    gpu_marks_[frame_slot].clear();
    used_queries_[frame_slot] = 0;
}

void FrameProfiler::ensure_pool_reset(VkCommandBuffer cmd)
{
    if (!created_ || !enabled_ || !cmd || pool_reset_recorded_)
        return;
    pool_.reset(cmd, record_slot_);
    pool_reset_recorded_ = true;
}

void FrameProfiler::end_host_frame(float frame_wall_ms)
{
    if (!created_ || !enabled_)
        return;

    std::lock_guard<std::mutex> lock(mu_);
    last_frame_ms_ = frame_wall_ms;
    ++sample_index_;

    // Union of CPU this frame + last resolved GPU names
    std::unordered_map<std::string, std::pair<float, float>> merged;
    for (const auto& kv : cpu_this_frame_)
        merged[kv.first].first = kv.second;
    for (const auto& kv : last_gpu_ms_)
        merged[kv.first].second = kv.second;

    float cpu_total = 0.f;
    float gpu_total = 0.f;
    for (const auto& kv : merged)
    {
        record_scope_sample_(kv.first.c_str(), kv.second.first, kv.second.second);
        cpu_total += kv.second.first;
        // GPU scopes may overlap (multi-queue); use max for bound heuristic
        if (kv.second.second > gpu_total)
            gpu_total = kv.second.second;
    }

    // Prefer wall for frame_ms; fall back to max work
    if (last_frame_ms_ <= 0.f)
        last_frame_ms_ = (std::max)(cpu_total, gpu_total);

    rebuild_snapshot_locked_();
    snapshot_.frame_ms = last_frame_ms_;
    snapshot_.cpu_ms = cpu_total;
    snapshot_.gpu_ms = gpu_total;
    snapshot_.bound = classify_bound(snapshot_.frame_ms, snapshot_.cpu_ms, snapshot_.gpu_ms);
    snapshot_.sample_index = sample_index_;
    snapshot_.valid = true;

    cpu_this_frame_.clear();
}

void FrameProfiler::add_cpu_ms(const char* name, float ms)
{
    if (!created_ || !enabled_ || !name || ms < 0.f)
        return;
    cpu_this_frame_[name] += ms;
}

uint32_t FrameProfiler::gpu_begin(VkCommandBuffer cmd,
                                  const char* name,
                                  VkPipelineStageFlags2 stage)
{
    if (!created_ || !enabled_ || !cmd || !name)
        return UINT32_MAX;

    ensure_pool_reset(cmd);
    if (next_query_ + 1 >= kQueriesPerFrame)
        return UINT32_MAX;

    const uint32_t begin_q = next_query_++;
    next_query_++; // reserve end
    pool_.write(cmd, record_slot_, begin_q, stage);

    GpuMark m{};
    m.name = name;
    m.begin_q = begin_q;
    m.end_q = begin_q + 1;
    m.complete = false;
    gpu_marks_[record_slot_].push_back(m);
    if (record_slot_ < used_queries_.size())
        used_queries_[record_slot_] = next_query_;
    return begin_q;
}

void FrameProfiler::gpu_end(VkCommandBuffer cmd,
                            uint32_t begin_query,
                            const char* name,
                            VkPipelineStageFlags2 stage)
{
    if (!created_ || !enabled_ || !cmd || begin_query == UINT32_MAX)
        return;

    const uint32_t end_q = begin_query + 1;
    pool_.write(cmd, record_slot_, end_q, stage);

    auto& marks = gpu_marks_[record_slot_];
    for (auto it = marks.rbegin(); it != marks.rend(); ++it)
    {
        if (it->begin_q == begin_query)
        {
            it->end_q = end_q;
            it->complete = true;
            if (name)
                it->name = name;
            break;
        }
    }
    if (record_slot_ < used_queries_.size() && end_q + 1 > used_queries_[record_slot_])
        used_queries_[record_slot_] = end_q + 1;
}

FrameProfiler::CpuScope::CpuScope(FrameProfiler* p, const char* name)
    : p_(p)
    , name_(name)
    , t0_(std::chrono::steady_clock::now())
{
    if (!p_ || !p_->enabled_ || !name_)
    {
        p_ = nullptr;
        name_ = nullptr;
    }
}

FrameProfiler::CpuScope::~CpuScope()
{
    if (!p_ || !name_)
        return;
    const auto t1 = std::chrono::steady_clock::now();
    const float ms =
        std::chrono::duration<float, std::milli>(t1 - t0_).count();
    p_->add_cpu_ms(name_, ms);
}

FrameProfiler::CpuScope::CpuScope(CpuScope&& o) noexcept
    : p_(o.p_)
    , name_(o.name_)
    , t0_(o.t0_)
{
    o.p_ = nullptr;
    o.name_ = nullptr;
}

FrameProfiler::GpuScope::GpuScope(FrameProfiler* p,
                                  VkCommandBuffer cmd,
                                  const char* name,
                                  VkPipelineStageFlags2 begin_stage,
                                  VkPipelineStageFlags2 end_stage)
    : p_(p)
    , cmd_(cmd)
    , name_(name)
    , end_stage_(end_stage)
{
    if (!p_ || !p_->enabled_ || !cmd_ || !name_)
    {
        p_ = nullptr;
        return;
    }
    begin_q_ = p_->gpu_begin(cmd_, name_, begin_stage);
    active_ = (begin_q_ != UINT32_MAX);
}

FrameProfiler::GpuScope::~GpuScope()
{
    if (!active_ || !p_)
        return;
    p_->gpu_end(cmd_, begin_q_, name_, end_stage_);
}

FrameProfiler::GpuScope::GpuScope(GpuScope&& o) noexcept
    : p_(o.p_)
    , cmd_(o.cmd_)
    , name_(o.name_)
    , end_stage_(o.end_stage_)
    , begin_q_(o.begin_q_)
    , active_(o.active_)
{
    o.active_ = false;
    o.p_ = nullptr;
}

void FrameProfiler::record_scope_sample_(const char* name, float cpu_ms, float gpu_ms)
{
    if (!name)
        return;
    ScopeAccum& a = accum_[name];
    if (a.cpu_ring.size() != kRingFrames)
    {
        a.cpu_ring.assign(kRingFrames, 0.f);
        a.gpu_ring.assign(kRingFrames, 0.f);
        a.ring_i = 0;
        a.ring_n = 0;
    }
    a.cpu_ring[a.ring_i] = cpu_ms;
    a.gpu_ring[a.ring_i] = gpu_ms;
    a.ring_i = (a.ring_i + 1) % kRingFrames;
    if (a.ring_n < kRingFrames)
        ++a.ring_n;
    a.cpu_sum += cpu_ms;
    a.gpu_sum += gpu_ms;
    ++a.samples;
}

float FrameProfiler::percentile_ring_(const std::vector<float>& ring, uint32_t n, float p)
{
    if (n == 0 || ring.empty())
        return 0.f;
    std::vector<float> tmp(ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(n));
    // When ring not full, only first n are valid; when full, all kRingFrames are valid
    // but order is circular — copy all ring_n samples from the ring storage correctly:
    if (n < ring.size())
    {
        // samples written 0..n-1 in order when filling
    }
    else
    {
        // full ring: values are valid but not sorted by time — copy whole ring
        tmp.assign(ring.begin(), ring.end());
    }
    std::sort(tmp.begin(), tmp.end());
    const float idx = p * static_cast<float>(tmp.size() - 1);
    const size_t i0 = static_cast<size_t>(idx);
    const size_t i1 = (std::min)(i0 + 1, tmp.size() - 1);
    const float t = idx - static_cast<float>(i0);
    return tmp[i0] * (1.f - t) + tmp[i1] * t;
}

void FrameProfiler::rebuild_snapshot_locked_()
{
    snapshot_ = {};
    snapshot_.scope_count = 0;

    // Stable-ish order: sort names
    std::vector<std::string> names;
    names.reserve(accum_.size());
    for (const auto& kv : accum_)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    for (const std::string& n : names)
    {
        if (snapshot_.scope_count >= kMaxFrameTimingScopes)
            break;
        const ScopeAccum& a = accum_[n];
        FrameTimingScope& s = snapshot_.scopes[snapshot_.scope_count++];
        copy_name(s.name, sizeof(s.name), n.c_str());
        // Last sample (most recent ring entry)
        if (a.ring_n > 0)
        {
            const uint32_t last =
                (a.ring_i + kRingFrames - 1) % kRingFrames;
            s.cpu_ms = a.cpu_ring[last];
            s.gpu_ms = a.gpu_ring[last];
            s.cpu_median_ms = percentile_ring_(a.cpu_ring, a.ring_n, 0.50f);
            s.gpu_median_ms = percentile_ring_(a.gpu_ring, a.ring_n, 0.50f);
            s.cpu_p95_ms = percentile_ring_(a.cpu_ring, a.ring_n, 0.95f);
            s.gpu_p95_ms = percentile_ring_(a.gpu_ring, a.ring_n, 0.95f);
        }
    }
}

void FrameProfiler::copy_snapshot(FrameTimingSnapshot& out) const
{
    std::lock_guard<std::mutex> lock(mu_);
    out = snapshot_;
}

FrameTimingSnapshot FrameProfiler::snapshot() const
{
    FrameTimingSnapshot s{};
    copy_snapshot(s);
    return s;
}
