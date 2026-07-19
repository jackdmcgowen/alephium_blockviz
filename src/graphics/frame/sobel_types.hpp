#pragma once

// Sobel highlight request DTOs (frame_loop builds; SobelAsyncPass consumes).
// No Vulkan device ownership here.

#include <cstdint>
#include <vector>

struct SobelFrameRequest
{
    enum class Mode {
        SelectionGold,
        ConfirmedTipsGreen,
        CyanFrontier, // unconfirmed children of domain frontier
        IncompleteTraceOrange
    };
    struct Layer
    {
        Mode mode = Mode::SelectionGold;
        std::vector<uint32_t> instance_indices;
    };
    std::vector<Layer> layers;
};
