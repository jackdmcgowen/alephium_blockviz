#pragma once

// Host (alephium_visualizer) owns application name + version.
// Passed into EngineCreateInfo.application → VkApplicationInfo.

#include "graphics/gpu_pub_lib.h"

namespace app_identity
{
inline constexpr const char* kName          = "Alephium BlockFlow";
inline constexpr uint32_t    kVersionMajor  = 0;
inline constexpr uint32_t    kVersionMinor  = 7;
inline constexpr uint32_t    kVersionPatch  = 0;

inline SoftwareIdentity make()
{
    return SoftwareIdentity{ kName, kVersionMajor, kVersionMinor, kVersionPatch };
}
} // namespace app_identity
