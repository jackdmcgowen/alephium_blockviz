#pragma once

// Device-lifetime sampler table: one VkSampler per filtering/address kind.
// Passes bind by SamplerFilter index — not pass-private sampler members.

#include <vulkan/vulkan.h>

#include <cstdint>

// Texture filtering / address mode identity (not pass-specific).
enum class SamplerFilter : uint8_t
{
    NearestClamp = 0, // point sample, clamp edges (Sobel depth/edge/outline)
    LinearClamp,      // bilinear, clamp (available for future sampled color)
    Count
};

// Low-level wrappers (raw vkCreateSampler only lives in sampler.cpp).
VkSampler create_sampler(VkDevice device, const VkSamplerCreateInfo& info);
void destroy_sampler(VkDevice device, VkSampler sampler);

// Creates all SamplerFilter kinds once after VkDevice exists.
class SamplerTable
{
public:
    void create(VkDevice device);
    void destroy(VkDevice device);

    VkSampler get(SamplerFilter f) const;
    bool ready() const { return device_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler samplers_[static_cast<size_t>(SamplerFilter::Count)]{};
};
