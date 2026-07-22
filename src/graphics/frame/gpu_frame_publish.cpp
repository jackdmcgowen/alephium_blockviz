#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"
#include "graphics/frame/frame_shared_state.hpp"
#include "graphics/frame/vertex_types.hpp"

#include <cstring>
#include <vector>
#include <string>

int GraphicsSystem::find_free_gpu_slot_unlocked() const
{
    // Triple buffer: always one free among {0,1,2} when reading and pending are distinct.
    for (int i = 0; i < kGpuSlots; ++i)
    {
        if (i != reading_slot_ && i != pending_slot_)
            return i;
    }
    // Degenerate: prefer overwriting pending (latest-wins still holds)
    return (pending_slot_ >= 0) ? pending_slot_ : 0;
}

void GraphicsSystem::submit_frame(const FrameSubmit& frame)
{
    publish_frame(frame, {}, {});
}

void GraphicsSystem::publish_frame(const FrameSubmit& frame,
                                 const std::vector<std::string>& pick_map,
                                 const std::vector<SobelOutlineInstance>& sobel_outlines)
{
    // Deep-copy only — no GPU work. Latest pending wins if GPU has not acquired yet.
    std::lock_guard<std::mutex> lock(submit_mutex_);

    const int write = find_free_gpu_slot_unlocked();
    GpuFrameSlot& slot = gpu_slots_[write];

    slot.instances.resize(frame.instance_count);
    if (frame.instance_count > 0 && frame.instances)
        std::memcpy(slot.instances.data(), frame.instances, frame.instance_count * sizeof(GpuInstance));
    slot.camera = frame.camera;
    slot.client_seq = frame.client_seq;
    slot.pick_map = pick_map;
    slot.sobel_outlines = sobel_outlines;

    pending_slot_ = write;
}

bool GraphicsSystem::apply_published_frame()
{
    int slot_idx = -1;
    {
        std::lock_guard<std::mutex> lock(submit_mutex_);
        if (pending_slot_ < 0)
            return false;
        reading_slot_ = pending_slot_;
        pending_slot_ = -1;
        slot_idx = reading_slot_;
    }

    const GpuFrameSlot& slot = gpu_slots_[slot_idx];
    instanceCount = frame_resources_.upload_instances(
        slot.instances.empty() ? nullptr : slot.instances.data(),
        slot.instances.size());
    frame_resources_.upload_camera(slot.camera, &g_viewProj);

    // Compact outline buffer: pos/scale from published instances, color from app.
    std::vector<InstanceData> outline_gpu;
    outline_gpu.reserve(slot.sobel_outlines.size());
    for (const SobelOutlineInstance& o : slot.sobel_outlines)
    {
        if (o.instance_index >= slot.instances.size())
            continue;
        if (outline_gpu.size() >= frame_graph::kMaxSobelInstances)
            break;
        const GpuInstance& src = slot.instances[o.instance_index];
        InstanceData d{};
        d.pos = src.pos;
        d.scale = src.scale;
        d.color = glm::vec3(o.color.r, o.color.g, o.color.b);
        d.alpha = o.color.a;
        outline_gpu.push_back(d);
    }
    frame_resources_.upload_outline_instances(
        outline_gpu.empty() ? nullptr : outline_gpu.data(), outline_gpu.size());

    pick_id_to_hash_ = slot.pick_map;
    gpu_frame_seq_ = slot.client_seq;
    return true;
}
