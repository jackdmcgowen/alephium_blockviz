#pragma once

// blockviz_engine owns engine name + version (public contract bump here).
// Used for VkApplicationInfo engine fields and startup logging.

#include "graphics/gpu_pub_lib.h"

namespace blockviz_engine
{
inline constexpr const char* kName         = "BlockvizEngine";
inline constexpr uint32_t    kVersionMajor = 0;
inline constexpr uint32_t    kVersionMinor = 7;
inline constexpr uint32_t    kVersionPatch = 0;

inline SoftwareIdentity identity()
{
    return SoftwareIdentity{ kName, kVersionMajor, kVersionMinor, kVersionPatch };
}
} // namespace blockviz_engine
