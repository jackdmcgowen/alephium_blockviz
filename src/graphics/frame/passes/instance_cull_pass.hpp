#pragma once

// GPU frustum cull of cube instances → compact SSBO + DrawIndexedIndirect args.
// Runs on CMP (or graphics if same family). Classic path reads compact list.
#include "graphics/frame/frame_graph/ipass.hpp"

#include <glm/glm.hpp>

namespace frame_graph
{

struct InstanceCullRecordParams
{
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkBuffer in_instances = VK_NULL_HANDLE;
    VkBuffer out_instances = VK_NULL_HANDLE;
    VkBuffer draw_args = VK_NULL_HANDLE; // VkDrawIndexedIndirectCommand
    uint32_t instance_count = 0;
    uint32_t max_instances = 0;
    // Frustum planes (same convention as camera.cpp Frustum).
    glm::vec4 planes[6]{};
    float half_extent = 1.05f; // match presenter instance_half_extents
    FrameProfiler* profiler = nullptr;
};

class InstanceCullPass final : public IPass
{
public:
    const char* name() const override { return "InstanceCullCMP"; }
    QueueType queue() const override { return QueueType::CMP; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;

    // Not used by graph executor yet — GraphicsSystem calls record_cull.
    void record(const PassRecordParams& /*ctx*/) override {}

    void declare_resources(std::vector<ResourceId>& /*reads*/,
                           std::vector<ResourceId>& /*writes*/) const override
    {
    }

    // Bind buffers, dispatch cull. Caller owns CB begin/end.
    void record_cull(const InstanceCullRecordParams& p);

    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    void write_descriptors_(VkBuffer in_buf, VkBuffer out_buf, VkBuffer draw_buf);

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkBuffer bound_in_ = VK_NULL_HANDLE;
    VkBuffer bound_out_ = VK_NULL_HANDLE;
    VkBuffer bound_draw_ = VK_NULL_HANDLE;
};

} // namespace frame_graph
