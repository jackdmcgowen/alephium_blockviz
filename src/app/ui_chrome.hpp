#pragma once

// Shared chrome layout for overlay + pick scene rect.
// Dual rails: Network (left) + Block (right), equal width.

namespace ui_chrome
{
inline constexpr float kRailWidth      = 340.f;
inline constexpr float kInspectorWidth = kRailWidth; // alias
// Status row + feed under the scene between rails.
inline constexpr float kToolbarHeight  = 200.f;

// Left/right rail width for a given framebuffer width.
// Cap so scene keeps a usable center band on small windows.
inline float rail_width(float fb_w)
{
    const float half = fb_w * 0.35f;
    float w = (half < kRailWidth) ? half : kRailWidth;
    // Ensure center scene has at least ~20% width.
    const float max_rail = fb_w * 0.4f;
    if (w * 2.f > max_rail)
        w = max_rail * 0.5f;
    if (w < 200.f && fb_w > 500.f)
        w = 200.f;
    return w;
}

// Backward-compatible alias (right Block rail).
inline float inspector_width(float fb_w)
{
    return rail_width(fb_w);
}

inline float scene_left(float fb_w)
{
    return rail_width(fb_w);
}

inline float scene_width(float fb_w)
{
    const float r = rail_width(fb_w);
    return (fb_w > 2.f * r) ? (fb_w - 2.f * r) : 1.f;
}
} // namespace ui_chrome
