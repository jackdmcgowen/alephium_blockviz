#pragma once

// Render-thread camera: Z-track scroll + LMB look + RMB pan + selection look-aim.
// Eye = pan + (0,0,scroll_z). Look uses smoothed yaw/pitch (exp tween).
#include "alph_block.hpp"
#include "graphics/camera.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

class CameraController
{
public:
    static constexpr float kEyeZMin   = -2000.f;
    static constexpr float kEyeZMax   =  2000.f;
    static constexpr float kPanMin    = -800.f;
    static constexpr float kPanMax    =  800.f;
    static constexpr float kEyeZStep  = 40.f;    // world units / second while key held
    static constexpr float kWheelStep = 25.f;    // world units per mouse-wheel notch
    static constexpr float kLookOmega   = 12.f;    // look angle tween rate
    static constexpr float kPanOmega    = 14.f;    // pan position tween rate
    static constexpr float kScrollOmega = 12.f;    // Z-scroll tween rate
    static constexpr float kLookSens  = 0.0045f; // radians per mouse pixel
    static constexpr float kPanSens   = 0.12f;   // world units per mouse pixel
    static constexpr float kPitchMin  = -1.35f;
    static constexpr float kPitchMax  =  1.35f;

    CameraController()
        : scroll_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
        , scroll_z_target_(scroll_z_)
    {
        camera_.forward = kForward;
        camera_.up = kUp;
        rebuild_camera_();
    }

    void set_aspect(float aspect)
    {
        camera_.aspect = (aspect > 1e-6f) ? aspect : 1.f;
    }

    void set_fov_y_rad(float fov) { camera_.fov_y_rad = fov; }
    void set_clip(float near_z, float far_z)
    {
        camera_.near_z = near_z;
        camera_.far_z = far_z;
    }

    // Smoothed eye Z (use for display / HUD).
    float scroll_z() const { return scroll_z_; }
    glm::vec3 pan() const { return pan_; }

    void set_scroll_z(float z)
    {
        scroll_z_target_ = std::clamp(z, kEyeZMin, kEyeZMax);
    }

    // Accumulate wheel / key deltas into the scroll *target* (tweened in tick).
    void nudge_scroll(float world_delta)
    {
        scroll_z_target_ =
            std::clamp(scroll_z_target_ + world_delta, kEyeZMin, kEyeZMax);
    }

    // RMB drag: pan in the camera right / camera-up plane.
    void add_pan_delta(float dx_px, float dy_px)
    {
        const glm::vec3 f = yaw_pitch_to_dir_(yaw_, pitch_);
        glm::vec3 right = glm::cross(kUp, f);
        const float rlen = glm::length(right);
        if (rlen < 1e-5f)
            right = glm::vec3(1.f, 0.f, 0.f);
        else
            right /= rlen;
        glm::vec3 cam_up = glm::normalize(glm::cross(f, right));

        // Grab-pan: content follows cursor (camera moves opposite to drag).
        pan_target_ -= right * (dx_px * kPanSens);
        pan_target_ -= cam_up * (dy_px * kPanSens);
        pan_target_.x = std::clamp(pan_target_.x, kPanMin, kPanMax);
        pan_target_.y = std::clamp(pan_target_.y, kPanMin, kPanMax);
        pan_target_.z = std::clamp(pan_target_.z, kPanMin, kPanMax);
    }

    // LMB drag: free look (Z scroll + pan offsets preserved).
    void add_look_delta(float dx_px, float dy_px)
    {
        look_engaged_ = false;
        free_look_ = true;

        yaw_target_   += dx_px * kLookSens;
        pitch_target_ += dy_px * kLookSens;
        pitch_target_  = std::clamp(pitch_target_, kPitchMin, kPitchMax);

        if (yaw_target_ >  glm::pi<float>())
            yaw_target_ -= glm::two_pi<float>();
        if (yaw_target_ < -glm::pi<float>())
            yaw_target_ += glm::two_pi<float>();
    }

    void set_look_target(const glm::vec3& world_pos, const std::string& hash)
    {
        if (hash.empty())
            return;
        const glm::vec3 eye = eye_position_();
        glm::vec3 dir = world_pos - eye;
        const float len = glm::length(dir);
        if (len < 0.05f)
            dir = kForward;
        else
            dir /= len;

        look_aim_hash_ = hash;
        look_engaged_ = true;
        free_look_ = false;
        dir_to_yaw_pitch_(dir, yaw_target_, pitch_target_);
        pitch_target_ = std::clamp(pitch_target_, kPitchMin, kPitchMax);
    }

    // Short RMB reset: home look (+Z) and pan back to XY origin (smooth via tick).
    void clear_look_target()
    {
        look_engaged_ = false;
        look_aim_hash_.clear();
        free_look_ = false;
        yaw_target_ = 0.f;
        pitch_target_ = 0.f;
        pan_target_ = glm::vec3(0.f);
    }

    const std::string& look_aim_hash() const { return look_aim_hash_; }
    bool look_engaged() const { return look_engaged_; }

    const Camera& tick(float dt_sec)
    {
        update_look_(dt_sec);
        update_pan_(dt_sec);
        update_scroll_(dt_sec);
        rebuild_camera_();
        return camera_;
    }

    const Camera& camera() const { return camera_; }

    CameraUBO ubo() const { return camera_.to_ubo(); }

    Frustum frustum() const { return camera_.frustum(); }

private:
    static inline const glm::vec3 kUp{ 0.f, -1.f, 0.f };
    static inline const glm::vec3 kForward{ 0.f, 0.f, 1.f };

    glm::vec3 eye_position_() const
    {
        // Z-track: scroll_z on Z; RMB pan offsets in XYZ (mostly lateral).
        return glm::vec3(pan_.x, pan_.y, scroll_z_ + pan_.z);
    }

    static void dir_to_yaw_pitch_(const glm::vec3& dir, float& yaw, float& pitch)
    {
        const glm::vec3 d = glm::normalize(dir);
        pitch = std::asin(std::clamp(-d.y, -1.f, 1.f));
        yaw = std::atan2(d.x, d.z);
    }

    static glm::vec3 yaw_pitch_to_dir_(float yaw, float pitch)
    {
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        return glm::normalize(glm::vec3(sy * cp, -sp, cy * cp));
    }

    static float exp_smooth_(float current, float target, float dt, float omega)
    {
        const float alpha = 1.f - std::exp(-omega * std::max(dt, 1e-4f));
        return current + (target - current) * std::clamp(alpha, 0.f, 1.f);
    }

    void update_look_(float dt)
    {
        float dyaw = yaw_target_ - yaw_;
        if (dyaw >  glm::pi<float>())
            dyaw -= glm::two_pi<float>();
        if (dyaw < -glm::pi<float>())
            dyaw += glm::two_pi<float>();
        const float yaw_goal = yaw_ + dyaw;

        yaw_   = exp_smooth_(yaw_, yaw_goal, dt, kLookOmega);
        pitch_ = exp_smooth_(pitch_, pitch_target_, dt, kLookOmega);
        pitch_ = std::clamp(pitch_, kPitchMin, kPitchMax);

        if (!look_dir_init_)
            look_dir_init_ = true;

        look_dir_ = yaw_pitch_to_dir_(yaw_, pitch_);
    }

    void update_pan_(float dt)
    {
        pan_.x = exp_smooth_(pan_.x, pan_target_.x, dt, kPanOmega);
        pan_.y = exp_smooth_(pan_.y, pan_target_.y, dt, kPanOmega);
        pan_.z = exp_smooth_(pan_.z, pan_target_.z, dt, kPanOmega);
    }

    void update_scroll_(float dt)
    {
        scroll_z_ = exp_smooth_(scroll_z_, scroll_z_target_, dt, kScrollOmega);
        scroll_z_ = std::clamp(scroll_z_, kEyeZMin, kEyeZMax);
    }

    void rebuild_camera_()
    {
        camera_.eye = eye_position_();
        glm::vec3 dir = look_dir_init_ ? look_dir_ : kForward;
        const float dlen = glm::length(dir);
        if (dlen < 1e-4f)
            dir = kForward;
        else
            dir /= dlen;
        camera_.forward = dir;
        camera_.up = kUp;
    }

    float scroll_z_ = 0.f;
    float scroll_z_target_ = 0.f;
    glm::vec3 pan_{ 0.f };
    glm::vec3 pan_target_{ 0.f };

    float yaw_ = 0.f;
    float pitch_ = 0.f;
    float yaw_target_ = 0.f;
    float pitch_target_ = 0.f;

    glm::vec3 look_dir_{ 0.f, 0.f, 1.f };
    bool      look_dir_init_ = false;
    std::string look_aim_hash_;
    bool      look_engaged_ = false;
    bool      free_look_ = false;

    Camera camera_{};
};
