#pragma once

// Explicit GPU buffer lifetimes (G1). Prefer BufferManager over ad-hoc create_buffer pairs.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <utility>
#include <vector>

struct BufferDesc
{
    VkDeviceSize          size         = 0;
    VkBufferUsageFlags    usage        = 0;
    VkMemoryPropertyFlags memory       = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const char*           debug_name   = nullptr; // optional; unused until debug utils labels
};

// Owns VkBuffer + VkDeviceMemory. Move-only.
class GpuBuffer
{
public:
    GpuBuffer() = default;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&& o) noexcept { *this = std::move(o); }
    GpuBuffer& operator=(GpuBuffer&& o) noexcept
    {
        if (this != &o)
        {
            // Caller must destroy before overwrite if valid — manager destroy preferred.
            buffer_ = o.buffer_;
            memory_ = o.memory_;
            size_ = o.size_;
            usage_ = o.usage_;
            memory_flags_ = o.memory_flags_;
            mapped_ = o.mapped_;
            o.buffer_ = VK_NULL_HANDLE;
            o.memory_ = VK_NULL_HANDLE;
            o.size_ = 0;
            o.mapped_ = nullptr;
        }
        return *this;
    }

    bool valid() const { return buffer_ != VK_NULL_HANDLE; }
    VkBuffer handle() const { return buffer_; }
    VkDeviceMemory memory() const { return memory_; }
    VkDeviceSize size() const { return size_; }
    VkBufferUsageFlags usage() const { return usage_; }
    VkMemoryPropertyFlags memory_flags() const { return memory_flags_; }
    void* mapped() const { return mapped_; }

    // Persistent map helpers (HOST_VISIBLE only). BufferManager may set mapped_ on create.
    void* map(VkDevice device, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void unmap(VkDevice device);

private:
    friend class BufferManager;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkBufferUsageFlags usage_ = 0;
    VkMemoryPropertyFlags memory_flags_ = 0;
    void* mapped_ = nullptr;
};

class BufferManager
{
public:
    BufferManager() = default;
    BufferManager(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props);

    void reset(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props);

    GpuBuffer create(const BufferDesc& desc);
    void destroy(GpuBuffer& buf);

    // Destroy all buffers still tracked (optional safety net).
    void destroy_all();

    VkDevice device() const { return device_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props_ = nullptr;
    std::vector<VkBuffer> live_; // weak tracking of handles for destroy_all
};
