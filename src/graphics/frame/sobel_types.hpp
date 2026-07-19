#pragma once

// Domain-agnostic Sobel outline list. App assigns colors; graphics has no role names.

#include "graphics/gpu_pub_lib.h"

#include <vector>

// Single pass: all outline cubes (already culled/filtered upstream).
// Colors are white Sobel edges multiplied by each instance's color.
struct SobelFrameRequest
{
    std::vector<SobelOutlineInstance> instances;
};
