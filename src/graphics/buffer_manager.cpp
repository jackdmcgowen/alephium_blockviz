#include "buffer_manager.hpp"
#include "gpu_prv_lib.h"

#include <algorithm>
#include <stdexcept>

BufferManager::BufferManager(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props)
{
    reset(device, mem_props);
}

void BufferManager::reset(VkDevice device, VkPhysicalDeviceMemoryProperties* mem_props)
{
    device_ = device;
    mem_props_ = mem_props;
}

GpuBuffer BufferManager::create(const BufferDesc& desc)
{
    if (!device_ || !mem_props_ || desc.size == 0)
        throw std::runtime_error("BufferManager::create: invalid args");

    GpuBuffer buf;
    buf.size_ = desc.size;
    buf.usage_ = desc.usage;
    buf.memory_flags_ = desc.memory;

    create_buffer(device_, mem_props_, desc.size, desc.usage, desc.memory,
                  buf.buffer_, buf.memory_);

    live_.push_back(buf.buffer_);
    (void)desc.debug_name;
    return buf;
}

void BufferManager::destroy(GpuBuffer& buf)
{
    if (!device_ || !buf.valid())
    {
        buf = GpuBuffer{};
        return;
    }

    if (buf.mapped_)
    {
        vkUnmapMemory(device_, buf.memory_);
        buf.mapped_ = nullptr;
    }

    live_.erase(std::remove(live_.begin(), live_.end(), buf.buffer_), live_.end());
    destroy_buffer(device_, buf.buffer_, buf.memory_);
    buf.buffer_ = VK_NULL_HANDLE;
    buf.memory_ = VK_NULL_HANDLE;
    buf.size_ = 0;
    buf.usage_ = 0;
    buf.memory_flags_ = 0;
}

void BufferManager::destroy_all()
{
    // Note: GpuBuffer objects may still hold handles; prefer explicit destroy(GpuBuffer&).
    live_.clear();
}

void* GpuBuffer::map(VkDevice device, VkDeviceSize offset, VkDeviceSize size)
{
    if (!device || memory_ == VK_NULL_HANDLE)
        return nullptr;
    if (mapped_)
        return mapped_;
    if (vkMapMemory(device, memory_, offset, size, 0, &mapped_) != VK_SUCCESS)
        mapped_ = nullptr;
    return mapped_;
}

void GpuBuffer::unmap(VkDevice device)
{
    if (!device || !mapped_ || memory_ == VK_NULL_HANDLE)
        return;
    vkUnmapMemory(device, memory_);
    mapped_ = nullptr;
}
