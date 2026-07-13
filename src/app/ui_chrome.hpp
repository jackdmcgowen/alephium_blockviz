#pragma once

// Shared chrome layout for overlay + pick scene rect (PR8).
// Explorer URLs live only in the overlay TU, not here.

namespace ui_chrome
{
inline constexpr float kInspectorWidth = 340.f;
// Shorter after retiring Scroll speed slider (status row + feed).
inline constexpr float kToolbarHeight  = 200.f;

// Right-rail width for a given framebuffer width.
inline float inspector_width(float fb_w)
{
    return (fb_w * 0.35f < kInspectorWidth) ? (fb_w * 0.35f) : kInspectorWidth;
}
} // namespace ui_chrome
