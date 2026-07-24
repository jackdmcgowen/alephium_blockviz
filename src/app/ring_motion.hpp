#pragma once

// Ring-only block motion: admit pop-in + rare Y-wave on history fill.
// Owned by ScenePresenter; keeps prepare free of motion state machines.
// See docs/planning/README.md and motion_easing.hpp.

#include "app/motion_easing.hpp"
#include "app/style_blockflow.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

class RingMotion
{
public:
    // Snapshot style + prune pops + expire wave. Call once per prepare instance phase.
    void begin_frame(float now, const StyleBlockflow& sty,
                     const std::unordered_set<std::string>& live_nodes)
    {
        now_          = now;
        pop_sec_      = sty.block_pop_sec;
        pop_overshoot_= sty.block_pop_overshoot;
        pop_max_      = static_cast<int>(sty.block_pop_max_concurrent + 0.5f);
        if (pop_max_ < 0)
            pop_max_ = 0;
        wave_dur_     = sty.wave_duration_sec;
        wave_pulse_   = sty.wave_pulse_sec;
        wave_amp_     = sty.wave_amplitude;
        wave_lift_    = sty.wave_lift;
        wave_cooldown_= sty.wave_cooldown_sec;

        if (wave_.start_sec >= 0.f && (now_ - wave_.start_sec) > wave_dur_)
        {
            last_wave_end_sec_ = now_;
            wave_.start_sec    = -1.f;
        }

        active_pops_ = 0;
        for (auto it = pop_birth_sec_.begin(); it != pop_birth_sec_.end();)
        {
            const float age      = now_ - it->second;
            const bool animating = age >= 0.f && age < pop_sec_;
            if (animating)
                ++active_pops_;
            if (!animating && live_nodes.count(it->first) == 0)
                it = pop_birth_sec_.erase(it);
            else
                ++it;
        }
    }

    // History fill slab just fulfilled — queue a rare ring wave (cooldown in after_drawn).
    void on_fill_fulfilled() { pending_wave_ = true; }

    // First visible-ring admit: 0 → overshoot → base_scale (N-cap).
    float pop_scale(const std::string& hash, float base_scale)
    {
        auto it = pop_birth_sec_.find(hash);
        if (it == pop_birth_sec_.end())
        {
            if (active_pops_ < pop_max_)
            {
                pop_birth_sec_[hash] = now_;
                ++active_pops_;
                return 0.f;
            }
            // Over cap: appear fully grown; mark done so we do not retry next frame.
            pop_birth_sec_[hash] = now_ - pop_sec_;
            return base_scale;
        }
        const float age = now_ - it->second;
        if (age <= 0.f)
            return 0.f;
        if (age >= pop_sec_)
            return base_scale;
        const float u = age / pop_sec_;
        float s = base_scale * motion::ease_out_back_overshoot(u, pop_overshoot_);
        return s > 0.f ? s : 0.f;
    }

    // +Y crest and optional scale pulse when a wave is live.
    void apply_wave(glm::vec3& pos, float& scale) const
    {
        if (wave_.start_sec < 0.f)
            return;
        if (wave_amp_ <= 0.f && wave_lift_ <= 0.f)
            return;
        const float elapsed = now_ - wave_.start_sec;
        const float env =
            motion::wave_envelope(elapsed, pos.y, wave_.y_lo, wave_.y_hi, wave_dur_,
                                  wave_pulse_);
        if (env <= 1e-4f)
            return;
        if (wave_lift_ > 0.f)
            pos.y += wave_lift_ * env;
        if (wave_amp_ > 0.f)
            scale *= (1.f + wave_amp_ * env);
    }

    // Resolve hash → world Y. Return false to skip.
    using YLookup = std::function<bool(const std::string& hash, float& y_out)>;

    // After drawn set is known: start pending wave using Y span of drawn cubes.
    void after_drawn(const std::unordered_set<std::string>& drawn, const YLookup& y_of,
                     float fallback_y_half_extent)
    {
        const bool want = pending_wave_;
        pending_wave_   = false;
        if (!want || drawn.empty())
            return;
        if (wave_.start_sec >= 0.f)
            return; // already mid-wave
        if ((now_ - last_wave_end_sec_) < wave_cooldown_)
            return;

        float y_lo = 1.e9f;
        float y_hi = -1.e9f;
        bool  any  = false;
        for (const std::string& h : drawn)
        {
            float y = 0.f;
            if (!y_of(h, y))
                continue;
            y_lo = std::min(y_lo, y);
            y_hi = std::max(y_hi, y);
            any  = true;
        }
        if (!any || (y_hi - y_lo) < 1e-3f)
        {
            y_lo = -fallback_y_half_extent;
            y_hi = fallback_y_half_extent;
        }
        wave_.start_sec = now_;
        wave_.y_lo      = y_lo;
        wave_.y_hi      = y_hi;
    }

private:
    struct Wave
    {
        float start_sec = -1.f;
        float y_lo      = 0.f;
        float y_hi      = 0.f;
    };

    float now_           = 0.f;
    float pop_sec_       = 0.28f;
    float pop_overshoot_ = 1.08f;
    int   pop_max_       = 48;
    int   active_pops_   = 0;
    float wave_dur_      = 0.65f;
    float wave_pulse_    = 0.28f;
    float wave_amp_      = 0.1f;
    float wave_lift_     = 0.4f;
    float wave_cooldown_ = 10.f;

    std::unordered_map<std::string, float> pop_birth_sec_;
    Wave  wave_{};
    float last_wave_end_sec_ = -1.e9f;
    bool  pending_wave_      = false;
};
