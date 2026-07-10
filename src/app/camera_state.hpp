#pragma once

// Client-owned camera params (K15 / PR8). Overlay mutates under mutex;
// engine reads a snapshot when building CameraUBO for submit_frame.
#include "alph_block.hpp"

#include <algorithm>
#include <mutex>

class CameraState
{
public:
    static constexpr float kEyeZMin  = -2000.f;
    static constexpr float kEyeZMax  =  2000.f;
    static constexpr float kEyeZStep = 40.f; // world units / second while key held
    static constexpr float kWheelStep = 25.f; // world units per mouse-wheel notch
    static constexpr float kMpsMin   = 1.f;
    static constexpr float kMpsMax   = 50.f;

    struct Snapshot
    {
        float eye_z = 0.f;
        float meters_per_second = 1.f;
    };

    CameraState()
        : eye_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
        , meters_per_second_(1.f)
    {
    }

    Snapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return { eye_z_, meters_per_second_ };
    }

    float eye_z() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return eye_z_;
    }

    float meters_per_second() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return meters_per_second_;
    }

    void set_eye_z(float z)
    {
        std::lock_guard<std::mutex> lock(mu_);
        eye_z_ = std::clamp(z, kEyeZMin, kEyeZMax);
    }

    void set_meters_per_second(float mps)
    {
        std::lock_guard<std::mutex> lock(mu_);
        meters_per_second_ = std::clamp(mps, kMpsMin, kMpsMax);
    }

    void nudge_eye_z(float delta)
    {
        std::lock_guard<std::mutex> lock(mu_);
        eye_z_ = std::clamp(eye_z_ + delta, kEyeZMin, kEyeZMax);
    }

private:
    mutable std::mutex mu_;
    float eye_z_;
    float meters_per_second_;
};
