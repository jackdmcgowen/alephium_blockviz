#pragma once

// Render-thread camera controller: manual Z scroll + selection look-aim.
// Eye Z is user scroll_z only (no auto-scroll rate — that distorted world Z).
#include "alph_block.hpp"
#include "graphics/camera.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

class CameraController
{
public:
    static constexpr float kEyeZMin   = -2000.f;
    static constexpr float kEyeZMax   =  2000.f;
    static constexpr float kEyeZStep  = 40.f;  // world units / second while key held
    static constexpr float kWheelStep = 25.f;  // world units per mouse-wheel notch
    static constexpr float kLookOmega = 10.f;  // slerp ease rate

    CameraController()
        : scroll_z_(static_cast<float>(-ALPH_LOOKBACK_WINDOW_SECONDS))
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

    float scroll_z() const { return scroll_z_; }

    void set_scroll_z(float z)
    {
        scroll_z_ = std::clamp(z, kEyeZMin, kEyeZMax);
    }

    void nudge_scroll(float world_delta)
    {
        scroll_z_ = std::clamp(scroll_z_ + world_delta, kEyeZMin, kEyeZMax);
    }

    // Hard-aim at world target (frozen direction until clear / new target).
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
        look_aim_dir_ = dir;
        look_aim_hash_ = hash;
        look_engaged_ = true;
    }

    void clear_look_target()
    {
        look_engaged_ = false;
        look_aim_hash_.clear();
    }

    const std::string& look_aim_hash() const { return look_aim_hash_; }
    bool look_engaged() const { return look_engaged_; }

    // Advance look slerp; rebuild Camera pose / matrices (no Z auto-scroll).
    const Camera& tick(float dt_sec)
    {
        update_look_(dt_sec);
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
        return glm::vec3(0.f, 0.f, scroll_z_);
    }

    void update_look_(float dt)
    {
        glm::vec3 desired = look_engaged_ ? look_aim_dir_ : kForward;
        const float dlen = glm::length(desired);
        if (dlen < 1e-4f)
            desired = kForward;
        else
            desired /= dlen;

        if (!look_dir_init_)
        {
            look_dir_ = desired;
            look_dir_init_ = true;
            return;
        }

        glm::vec3 from = look_dir_;
        const float flen = glm::length(from);
        if (flen < 1e-4f)
            from = kForward;
        else
            from /= flen;

        float alpha = 1.f - std::exp(-kLookOmega * std::max(dt, 1e-4f));
        alpha = std::clamp(alpha, 0.f, 1.f);

        const float dot = glm::clamp(glm::dot(from, desired), -1.f, 1.f);
        if (dot > 0.9995f)
        {
            look_dir_ = desired;
            return;
        }

        const glm::quat q_delta = glm::rotation(from, desired);
        const glm::quat q_step = glm::slerp(glm::identity<glm::quat>(), q_delta, alpha);
        look_dir_ = glm::normalize(q_step * from);
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

    glm::vec3 look_dir_{ 0.f, 0.f, 1.f };
    bool      look_dir_init_ = false;
    glm::vec3 look_aim_dir_{ 0.f, 0.f, 1.f };
    std::string look_aim_hash_;
    bool      look_engaged_ = false;

    Camera camera_{};
};
