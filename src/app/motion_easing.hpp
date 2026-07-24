#pragma once

// Header-only easing for BlockFlow motion (PR2+).
// Prefer ease-out / ease-in-out; overshoot only via ease_out_back on pop-in.
// See docs/planning/README.md motion language.

#include <algorithm>
#include <cmath>

namespace motion
{

inline float clamp01(float t) noexcept
{
    return t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
}

inline float ease_linear(float t) noexcept
{
    return clamp01(t);
}

// 1 - (1-t)^3
inline float ease_out_cubic(float t) noexcept
{
    t = clamp01(t);
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

inline float ease_in_cubic(float t) noexcept
{
    t = clamp01(t);
    return t * t * t;
}

inline float ease_in_out_cubic(float t) noexcept
{
    t = clamp01(t);
    if (t < 0.5f)
        return 4.f * t * t * t;
    const float u = -2.f * t + 2.f;
    return 1.f - 0.5f * u * u * u;
}

// Penner easeOutBack. Default s≈1.70158 → ~10% overshoot past 1, ends at 1.
// f(0)=0, f(1)=1, peaks above 1 near the end.
inline float ease_out_back(float t, float s = 1.70158f) noexcept
{
    t = clamp01(t);
    const float inv = t - 1.f;
    return 1.f + inv * inv * ((s + 1.f) * inv + s);
}

// Scale multiplier 0 → ~overshoot → 1.0 (block admit pop-in).
// overshoot is the peak factor (e.g. 1.08). Maps to Penner s via known
// peak≈1.1002 at s=1.70158 (exact peak = 1 + 4 s^3 / (27 (s+1)^2)).
inline float ease_out_back_overshoot(float t, float overshoot = 1.08f) noexcept
{
    constexpr float kRefS    = 1.70158f;
    constexpr float kRefPeak = 1.100175f; // peak at kRefS
    const float peak = overshoot > 1.001f ? overshoot : 1.08f;
    const float s    = kRefS * ((peak - 1.f) / (kRefPeak - 1.f));
    return ease_out_back(t, s);
}

// 0 → 1 → 0 over t∈[0,1]; peak at 0.5. For wave / tip pulses (ease-in-out).
inline float bump_in_out(float t) noexcept
{
    t = clamp01(t);
    if (t <= 0.5f)
        return ease_in_out_cubic(t * 2.f);
    return ease_in_out_cubic(2.f - t * 2.f);
}

// Z-staggered wave envelope at block_z.
// elapsed = now - wave_start; local_u 0 at z_hi (older) → 1 at z_lo (newer).
// Returns 0..1 amplitude; 0 outside the traveling pulse.
inline float wave_envelope(float elapsed, float block_z, float z_lo, float z_hi,
                           float duration_sec, float pulse_sec) noexcept
{
    if (elapsed < 0.f || duration_sec < 1e-4f || pulse_sec < 1e-4f)
        return 0.f;
    const float zspan = z_hi - z_lo;
    const float local =
        zspan > 1e-3f ? clamp01((z_hi - block_z) / zspan) : 0.f;
    const float travel = std::max(0.f, duration_sec - pulse_sec);
    const float delay  = local * travel;
    const float age    = elapsed - delay;
    if (age < 0.f || age > pulse_sec)
        return 0.f;
    return bump_in_out(age / pulse_sec);
}

} // namespace motion
