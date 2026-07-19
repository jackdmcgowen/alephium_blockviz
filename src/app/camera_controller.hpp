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
    static constexpr float kEyeZStep  = 200.f;   // world units / second while key held
    static constexpr float kWheelStep = 100.f;   // world units per mouse-wheel notch
    static constexpr float kLookOmega   = 12.f;
    static constexpr float kPanOmega    = 14.f;
    // Scroll Z: linear approach toward target (not exp spring, not hard snap).
    // Fast enough for large segment jumps and wheel/key travel.
    static constexpr float kScrollLinearSpeed = 500.f; // world units / second
    static constexpr float kLookSens  = 0.0045f;
    static constexpr float kPanSens   = 0.12f;
    static constexpr float kPitchMin  = -1.35f;
    static constexpr float kPitchMax  =  1.35f;
    // Follow: 1 world unit per real second (matches layout time axis).
    static constexpr float kTimelineMetersPerSecond = 1.f;
    // Side preset: eye offset from timeline axis (layout base_radius≈20).
    static constexpr float kSideRadius = 70.f;
    static constexpr float kSidePitch  = 0.10f;
    // Up/Down in Side: orbit rate (rad/s while key held).
    static constexpr float kOrbitRadPerSec = 1.15f;
    static constexpr float kViewBlendSec   = 0.40f;

    // End = look along +Z into polar ring; Side = profile timeline along Z.
    enum class ViewPreset : int { End = 0, Side = 1 };

    struct CameraPose
    {
        float     scroll_z = 0.f;
        float     scroll_z_target = 0.f;
        glm::vec3 pan{ 0.f };
        glm::vec3 pan_target{ 0.f };
        float     yaw = 0.f;
        float     pitch = 0.f;
        float     yaw_target = 0.f;
        float     pitch_target = 0.f;
        float     orbit_rad = 0.f;
        bool      timeline_attached = true;
        bool      valid = false; // false until first visit saved
    };

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

    // Snap both current and target (minimap page / edge wrap) so network
    // lookback index sees the new Z without scroll lerp lag.
    void set_scroll_z_immediate(float z)
    {
        detach_timeline_();
        const float c = std::clamp(z, z_min_, z_max_);
        scroll_z_target_ = c;
        scroll_z_ = c;
    }

    // Move scroll target; actual eye Z linear-approaches in update_scroll_.
    void nudge_scroll(float world_delta)
    {
        detach_timeline_();
        scroll_z_target_ =
            std::clamp(scroll_z_target_ + world_delta, z_min_, z_max_);
    }

    // Side view only: rotate eye around timeline Z axis (radians).
    void nudge_orbit(float delta_rad)
    {
        if (view_preset_ != ViewPreset::Side || blending_)
            return;
        free_look_ = false;
        look_engaged_ = false;
        orbit_rad_ += delta_rad;
        // Keep finite.
        if (orbit_rad_ > glm::pi<float>())
            orbit_rad_ -= glm::two_pi<float>();
        if (orbit_rad_ < -glm::pi<float>())
            orbit_rad_ += glm::two_pi<float>();
        apply_view_preset_targets_(ViewPreset::Side);
    }

    float orbit_rad() const { return orbit_rad_; }

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

        // Drag content with the pointer (not opposite): positive dx pans world right.
        pan_target_ += right * (dx_px * kPanSens);
        pan_target_ += cam_up * (dy_px * kPanSens);
        pan_target_.x = std::clamp(pan_target_.x, kPanMin, kPanMax);
        pan_target_.y = std::clamp(pan_target_.y, kPanMin, kPanMax);
        pan_target_.z = std::clamp(pan_target_.z, kPanMin, kPanMax);
    }

    void add_look_delta(float dx_px, float dy_px)
    {
        look_engaged_ = false;
        free_look_ = true;

        // Inverted-Y camera (up = -Y): drag right → look right; drag up → look up.
        yaw_target_   -= dx_px * kLookSens;
        pitch_target_ -= dy_px * kLookSens;
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

    // Short RMB: home look/pan for current preset + reattach to live timeline Z.
    void clear_look_target()
    {
        look_engaged_ = false;
        look_aim_hash_.clear();
        free_look_ = false;
        apply_view_preset_targets_(view_preset_);
        reattach_timeline();
    }

    void reattach_timeline()
    {
        timeline_attached_ = true;
        scroll_z_target_ = std::clamp(live_scroll_z_, z_min_, z_max_);
    }

    void set_view_preset(ViewPreset p)
    {
        if (p == view_preset_ && !blending_)
            return;
        look_engaged_ = false;
        look_aim_hash_.clear();
        free_look_ = false;

        // Remember current live pose under the outgoing preset.
        save_pose_(view_preset_);

        CameraPose to{};
        if (pose_[static_cast<int>(p)].valid)
            to = pose_[static_cast<int>(p)];
        else
        {
            // First visit: defaults for that preset (keep current scroll_z).
            to.scroll_z = scroll_z_;
            to.scroll_z_target = scroll_z_target_;
            to.timeline_attached = timeline_attached_;
            to.orbit_rad = (p == ViewPreset::Side) ? 0.f : orbit_rad_;
            to.valid = true;
            // Apply default look/pan into a temp by setting targets then snapshot.
            const ViewPreset prev = view_preset_;
            view_preset_ = p;
            orbit_rad_ = to.orbit_rad;
            apply_view_preset_targets_(p);
            to.pan = pan_target_;
            to.pan_target = pan_target_;
            to.yaw = yaw_target_;
            to.pitch = pitch_target_;
            to.yaw_target = yaw_target_;
            to.pitch_target = pitch_target_;
            view_preset_ = prev;
        }

        blend_from_ = capture_live_pose_();
        blend_to_ = to;
        blending_ = true;
        blend_t_ = 0.f;
        view_preset_ = p;
        // Immediate target so mid-blend scroll attach uses correct mode.
        apply_pose_targets_(to);
    }

    void toggle_view_preset()
    {
        set_view_preset(view_preset_ == ViewPreset::Side ? ViewPreset::End
                                                         : ViewPreset::Side);
    }

    ViewPreset view_preset() const { return view_preset_; }

    const std::string& look_aim_hash() const { return look_aim_hash_; }
    bool look_engaged() const { return look_engaged_; }

    const Camera& tick(float dt_sec)
    {
        if (blending_)
        {
            blend_t_ += dt_sec / kViewBlendSec;
            if (blend_t_ >= 1.f)
            {
                blend_t_ = 1.f;
                blending_ = false;
                apply_pose_full_(blend_to_);
                pose_[static_cast<int>(view_preset_)] = blend_to_;
                pose_[static_cast<int>(view_preset_)].valid = true;
            }
            else
            {
                apply_pose_lerp_(blend_from_, blend_to_, blend_t_);
                rebuild_camera_();
                return camera_;
            }
        }
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

    CameraPose capture_live_pose_() const
    {
        CameraPose p;
        p.scroll_z = scroll_z_;
        p.scroll_z_target = scroll_z_target_;
        p.pan = pan_;
        p.pan_target = pan_target_;
        p.yaw = yaw_;
        p.pitch = pitch_;
        p.yaw_target = yaw_target_;
        p.pitch_target = pitch_target_;
        p.orbit_rad = orbit_rad_;
        p.timeline_attached = timeline_attached_;
        p.valid = true;
        return p;
    }

    void save_pose_(ViewPreset preset)
    {
        pose_[static_cast<int>(preset)] = capture_live_pose_();
    }

    void apply_pose_targets_(const CameraPose& p)
    {
        scroll_z_target_ = p.scroll_z_target;
        pan_target_ = p.pan_target;
        yaw_target_ = p.yaw_target;
        pitch_target_ = p.pitch_target;
        orbit_rad_ = p.orbit_rad;
        timeline_attached_ = p.timeline_attached;
    }

    void apply_pose_full_(const CameraPose& p)
    {
        scroll_z_ = p.scroll_z;
        scroll_z_target_ = p.scroll_z_target;
        pan_ = p.pan;
        pan_target_ = p.pan_target;
        yaw_ = p.yaw;
        pitch_ = p.pitch;
        yaw_target_ = p.yaw_target;
        pitch_target_ = p.pitch_target;
        orbit_rad_ = p.orbit_rad;
        timeline_attached_ = p.timeline_attached;
        look_dir_ = yaw_pitch_to_dir_(yaw_, pitch_);
        look_dir_init_ = true;
    }

    static float lerp_angle_(float a, float b, float t)
    {
        float d = b - a;
        if (d > glm::pi<float>())
            d -= glm::two_pi<float>();
        if (d < -glm::pi<float>())
            d += glm::two_pi<float>();
        return a + d * t;
    }

    void apply_pose_lerp_(const CameraPose& a, const CameraPose& b, float t)
    {
        t = std::clamp(t, 0.f, 1.f);
        scroll_z_ = glm::mix(a.scroll_z, b.scroll_z, t);
        scroll_z_target_ = glm::mix(a.scroll_z_target, b.scroll_z_target, t);
        pan_ = glm::mix(a.pan, b.pan, t);
        pan_target_ = glm::mix(a.pan_target, b.pan_target, t);
        yaw_ = lerp_angle_(a.yaw, b.yaw, t);
        pitch_ = glm::mix(a.pitch, b.pitch, t);
        yaw_target_ = lerp_angle_(a.yaw_target, b.yaw_target, t);
        pitch_target_ = glm::mix(a.pitch_target, b.pitch_target, t);
        orbit_rad_ = lerp_angle_(a.orbit_rad, b.orbit_rad, t);
        timeline_attached_ = (t < 0.5f) ? a.timeline_attached : b.timeline_attached;
        look_dir_ = yaw_pitch_to_dir_(yaw_, pitch_);
        look_dir_init_ = true;
    }

    void apply_view_preset_targets_(ViewPreset p)
    {
        if (p == ViewPreset::Side)
        {
            // Orbit around Z: eye on circle in XY looking at timeline axis.
            const float c = std::cos(orbit_rad_);
            const float s = std::sin(orbit_rad_);
            pan_target_ = glm::vec3(kSideRadius * c, kSideRadius * s, 0.f);
            // Look toward origin in XY (and slight pitch for readability).
            const glm::vec3 dir = glm::normalize(glm::vec3(-c, -s, 0.05f));
            dir_to_yaw_pitch_(dir, yaw_target_, pitch_target_);
            pitch_target_ = std::clamp(pitch_target_ + kSidePitch * 0.25f, kPitchMin, kPitchMax);
        }
        else
        {
            pan_target_ = glm::vec3(0.f);
            yaw_target_ = 0.f;
            pitch_target_ = 0.f;
        }
        pitch_target_ = std::clamp(pitch_target_, kPitchMin, kPitchMax);
    }

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
    ViewPreset view_preset_ = ViewPreset::End;
    float      orbit_rad_ = 0.f; // Side: angle of eye around Z (0 = +X)

    CameraPose pose_[2]{};
    bool       blending_ = false;
    float      blend_t_ = 0.f;
    CameraPose blend_from_{};
    CameraPose blend_to_{};

    Camera camera_{};
};
