#pragma once

// Render-thread camera: Z-track scroll + LMB look + RMB pan + selection look-aim.
// Z is wall-clock seconds on the block timeline (matches layout meters_per_second=1).
// Attached: auto-follows live tip at 1 s/s. User Z input detaches; short RMB recouples.
#include "domain/alph_block.hpp"
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
    // Soft pan bounds only (Z uses timeline limits, not ±2000).
    static constexpr float kPanMin    = -800.f;
    static constexpr float kPanMax    =  800.f;
    static constexpr float kEyeZStep  = 40.f;    // world units / second while key held
    static constexpr float kWheelStep = 25.f;    // world units per mouse-wheel notch
    static constexpr float kLookOmega   = 12.f;
    static constexpr float kPanOmega    = 14.f;
    // Scroll Z: linear approach toward target (not exp spring, not hard snap).
    // Fast enough to track continuous live tip (~1 m/s) and smooth wheel jumps.
    static constexpr float kScrollLinearSpeed = 120.f; // world units / second
    static constexpr float kLookSens  = 0.0045f;
    static constexpr float kPanSens   = 0.12f;
    static constexpr float kPitchMin  = -1.35f;
    static constexpr float kPitchMax  =  1.35f;
    // Follow: 1 world unit per real second (matches layout time axis).
    static constexpr float kTimelineMetersPerSecond = 1.f;

    CameraController()
        : scroll_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
        , scroll_z_target_(scroll_z_)
        , live_scroll_z_(scroll_z_)
        , z_min_(-1.e9f)
        , z_max_(1.e9f)
        , timeline_attached_(true)
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

    // Timeline Z limits (newer more negative with current layout convention).
    void set_scroll_z_limits(float z_min, float z_max)
    {
        if (z_min > z_max)
            std::swap(z_min, z_max);
        z_min_ = z_min;
        z_max_ = z_max;
        scroll_z_target_ = std::clamp(scroll_z_target_, z_min_, z_max_);
        scroll_z_ = std::clamp(scroll_z_, z_min_, z_max_);
    }

    // Live tip-window Z (attached follow target). Updated each frame by host/render.
    void set_live_scroll_z(float z)
    {
        live_scroll_z_ = std::clamp(z, z_min_, z_max_);
    }

    float scroll_z() const { return scroll_z_; }
    float scroll_z_target() const { return scroll_z_target_; }
    float live_scroll_z() const { return live_scroll_z_; }
    bool  timeline_attached() const { return timeline_attached_; }
    glm::vec3 pan() const { return pan_; }

    void set_scroll_z(float z)
    {
        detach_timeline_();
        scroll_z_target_ = std::clamp(z, z_min_, z_max_);
    }

    // Move scroll target; actual eye Z linear-approaches in update_scroll_.
    void nudge_scroll(float world_delta)
    {
        detach_timeline_();
        scroll_z_target_ =
            std::clamp(scroll_z_target_ + world_delta, z_min_, z_max_);
    }

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

        pan_target_ -= right * (dx_px * kPanSens);
        pan_target_ -= cam_up * (dy_px * kPanSens);
        pan_target_.x = std::clamp(pan_target_.x, kPanMin, kPanMax);
        pan_target_.y = std::clamp(pan_target_.y, kPanMin, kPanMax);
        pan_target_.z = std::clamp(pan_target_.z, kPanMin, kPanMax);
    }

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

    // Short RMB: home look + pan origin + reattach to live timeline Z.
    void clear_look_target()
    {
        look_engaged_ = false;
        look_aim_hash_.clear();
        free_look_ = false;
        yaw_target_ = 0.f;
        pitch_target_ = 0.f;
        pan_target_ = glm::vec3(0.f);
        reattach_timeline();
    }

    void reattach_timeline()
    {
        timeline_attached_ = true;
        scroll_z_target_ = std::clamp(live_scroll_z_, z_min_, z_max_);
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

    void detach_timeline_() { timeline_attached_ = false; }

    glm::vec3 eye_position_() const
    {
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

    // Constant-speed linear approach: smooth continuous motion (ms-scale when
    // live target is continuous). Avoids exp-spring lag and integer-second jumps.
    static float linear_approach_(float current, float target, float dt, float speed)
    {
        const float d = target - current;
        const float max_step = speed * std::max(dt, 0.f);
        if (std::abs(d) <= max_step)
            return target;
        return current + (d > 0.f ? max_step : -max_step);
    }

    void update_scroll_(float dt)
    {
        if (timeline_attached_)
            scroll_z_target_ = std::clamp(live_scroll_z_, z_min_, z_max_);

        scroll_z_target_ = std::clamp(scroll_z_target_, z_min_, z_max_);
        scroll_z_ = linear_approach_(scroll_z_, scroll_z_target_, dt, kScrollLinearSpeed);
        scroll_z_ = std::clamp(scroll_z_, z_min_, z_max_);
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
    float live_scroll_z_ = 0.f;
    float z_min_ = -1.e9f;
    float z_max_ = 1.e9f;
    bool  timeline_attached_ = true;

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
