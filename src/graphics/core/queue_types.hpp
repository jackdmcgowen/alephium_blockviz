#pragma once

// Logical queue roles for the device (not raw family indices).
// Family indices are resolved at device create and stored in DeviceQueues.
#include <vulkan/vulkan.h>

#include <cstdint>

enum class QueueType : uint32_t
{
    _3D = 0, // graphics + present
    TX  = 1, // transfer (DMA) — dedicated when available
    CMP = 2, // compute — dedicated when available
    Count
};

inline constexpr uint32_t queue_type_count()
{
    return static_cast<uint32_t>(QueueType::Count);
}

inline const char* queue_type_name(QueueType t)
{
    switch (t)
    {
    case QueueType::_3D: return "_3D";
    case QueueType::TX:  return "TX";
    case QueueType::CMP: return "CMP";
    default:             return "unknown";
    }
}

// Resolved queues + owning family indices after create_device.
struct DeviceQueues
{
    VkQueue  handle[static_cast<uint32_t>(QueueType::Count)]{};
    uint32_t family[static_cast<uint32_t>(QueueType::Count)]{};
    bool     dedicated_tx  = false; // TX is not the graphics family
    bool     dedicated_cmp = false; // CMP is not the graphics family

    VkQueue get(QueueType t) const
    {
        return handle[static_cast<uint32_t>(t)];
    }

    uint32_t family_index(QueueType t) const
    {
        return family[static_cast<uint32_t>(t)];
    }

    bool same_family(QueueType a, QueueType b) const
    {
        return family_index(a) == family_index(b);
    }
};
