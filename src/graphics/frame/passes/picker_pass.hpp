#pragma once

// GPU pick pass: R32_UINT object id + private 1× depth. Owns picker PSO privately (IPass).
// Pick policy (when to pick, hash maps, selection) stays on GraphicsSystem.
#include "graphics/frame/frame_graph/ipass.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/buffer_manager.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace frame_graph
{

inline constexpr VkFormat kPickerColorFormat = VK_FORMAT_R32_UINT;
inline constexpr uint32_t kPickerInvalidId   = ~0u;
inline constexpr VkExtent2D kPickerReadExtent = { 1, 1 };

class PickerPass final : public IPass
{
public:
    const char* name() const override { return "Picker"; }
    QueueType queue() const override { return QueueType::_3D; }

    void create(const PassCreateInfo& info) override;
    void destroy(VkDevice device) override;
    void recreate(const PassCreateInfo& info) override;

    void record(const PassRecordParams& ctx) override;

    void declare_resources(std::vector<ResourceId>& reads,
                           std::vector<ResourceId>& writes) const override;

    // CPU readback after GPU work for the frame has finished.
    uint32_t read_object_id(VkDevice device) const;

    VkFormat color_format() const { return kPickerColorFormat; }
    bool ready() const { return pipeline_ != VK_NULL_HANDLE && image_ != VK_NULL_HANDLE; }

private:
    void create_pipeline_(const PassCreateInfo& info);
    void destroy_pipeline_(VkDevice device);
    void create_resources_(const PassCreateInfo& info);
    void destroy_resources_(VkDevice device);
    void create_staging_(BufferManager* buffers);
    void destroy_staging_();

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;

    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;

    // Private 1× depth — never the MSAA scene depth (validation: sample counts must match).
    VkImage depth_image_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;

    BufferManager* buffers_ = nullptr;
    GpuBuffer staging_;
};

} // namespace frame_graph
