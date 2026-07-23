#include "app/pch.h"
#include "app/scene_presenter.hpp"
#include "app/style_blockflow.hpp"

#include "domain/alph_block.hpp"
#include "graphics/camera.hpp"
#include "graphics/debug/debug_drawer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <functional>
#include <optional>

#include <glm/glm.hpp>

namespace
{
// World units per second of block timestamp (matches camera scroll_z units).
// Keep spacing: do not change meters_per_second or base_radius.
float meters_per_second = 1.0f;
constexpr float kLayoutBaseRadius = 20.0f;

// Colors from StyleBlockflow::global() (brand tokens / style_blockflow.json).
inline const StyleBlockflow& sty() { return StyleBlockflow::global(); }
inline glm::vec4 kUnconfirmedArrowColor() { return sty().unconfirmed_red; }
inline glm::vec4 kGreenArrowColor() { return sty().tip_green; }
inline glm::vec4 kDeathArrowColor() { return sty().death_red; }
inline glm::vec4 kSelectionArrowColor() { return sty().select_gold; }
inline glm::vec4 kHoverArrowColor()
{
    glm::vec4 c = sty().select_gold;
    c.a *= 0.45f;
    return c;
}
// Closed G barriers (not open live tip). Stronger α so F12/PNG and on-screen match.
const glm::vec4 kBarrierPlaneColor(0.30f, 0.82f, 1.00f, 0.16f);
const glm::vec4 kBarrierPlaneDim(0.30f, 0.78f, 0.98f, 0.12f);
// History network fill volumes (64s subseg). In-flight vs fading must read differently.
const glm::vec4 kFillVolumeInflight(0.55f, 0.56f, 0.60f, 0.16f);
const glm::vec4 kFillVolumeFading(0.50f, 0.52f, 0.56f, 0.07f);
constexpr int kMaxFullAlphaFillVolumes = 4; // matches HttpIoPool interval inflight

// BFS confirm rays: muted brand family (not high-chroma rainbow).
glm::vec4 bfs_thread_color(int thread_id)
{
    const StyleBlockflow& s = sty();
    const glm::vec4 palette[] = {
        s.unconfirmed_red, s.tip_green, s.incomplete_amber, s.select_gold,
        s.unconfirmed_red * 0.75f + glm::vec4(0.1f, 0.1f, 0.12f, 0.f),
        s.tip_green * 0.7f + glm::vec4(0.15f, 0.12f, 0.1f, 0.f),
        s.incomplete_amber * 0.65f + glm::vec4(0.12f, 0.1f, 0.15f, 0.f),
    };
    glm::vec4 c = palette[thread_id % 7];
    c.a = std::min(c.a, 0.75f) * s.bfs_alpha_scale;
    return c;
}

// Shared helper: layout, planes, and segment cull all use this Z mapping.
inline float ts_to_z(int64_t ts_ms, int64_t origin_ms, float mps)
{
    return -static_cast<float>(ts_ms - origin_ms) * 0.001f * mps;
}

bool inset_segment(const glm::vec3& from, const glm::vec3& to, float clearance,
                   glm::vec3& from_out, glm::vec3& to_out)
{
    const glm::vec3 delta = to - from;
    const float len = glm::length(delta);
    if (len < 2.0f * clearance + 1e-4f)
        return false;
    const glm::vec3 dir = delta / len;
    from_out = from + dir * clearance;
    to_out = to - dir * clearance;
    return true;
}

// Key: listing block (owns deps[]) then dependency. Arrow draws listing â†’ dep.
std::string tip_edge_key(const std::string& listing, const std::string& dep)
{
    return std::string("t|") + listing + '|' + dep;
}

bool parse_tip_edge_key(const std::string& key, std::string& listing, std::string& dep)
{
    if (key.size() < 3 || key[0] != 't' || key[1] != '|')
        return false;
    const size_t p1 = key.find('|', 2);
    if (p1 == std::string::npos)
        return false;
    listing = key.substr(2, p1 - 2);
    dep = key.substr(p1 + 1);
    return !listing.empty() && !dep.empty();
}
} // namespace

ScenePresenter::ScenePresenter(BlockScene& scene)
    : scene_(scene)
{
    StyleBlockflow::global().try_load();
}

float ScenePresenter::now_sec_() const
{
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - clock0_).count();
}

float ScenePresenter::ephemeral_grow_u_(const std::string& key, float stagger_delay,
                                        std::unordered_set<std::string>& seen,
                                        float grow_sec)
{
    seen.insert(key);
    const float now = now_sec_();
    auto it = ephemeral_birth_sec_.find(key);
    if (it == ephemeral_birth_sec_.end())
    {
        ephemeral_birth_sec_[key] = now + stagger_delay;
        it = ephemeral_birth_sec_.find(key);
    }
    const float age = now - it->second;
    if (age <= 0.f)
        return 0.f;
    const float g = grow_sec > 1e-4f ? grow_sec : kArrowGrowSec;
    return std::clamp(age / g, 0.f, 1.f);
}

void ScenePresenter::clear_selection_deps_()
{
    sel_dep_ = SelectionDepTrace{};
    // Drop sticky selection ephemeral arrows from prior root.
    for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end();)
    {
        if (!it->first.empty() && (it->first[0] == 's' || it->first[0] == 'w'))
            it = ephemeral_birth_sec_.erase(it);
        else
            ++it;
    }
}

void ScenePresenter::collect_selection_dep_force_(std::unordered_set<std::string>& out) const
{
    for (const std::string& h : sel_dep_.nodes)
        if (!h.empty())
            out.insert(h);
}

void ScenePresenter::rebuild_selection_dep_one_hop_(const std::string& root_hash,
                                                    bool animate_grow)
{
    clear_selection_deps_();
    if (root_hash.empty())
        return;

    sel_dep_.root = root_hash;
    sel_dep_.nodes.push_back(root_hash);
    sel_dep_.node_set.insert(root_hash);

    std::vector<std::string> deps = sorted_deps_(root_hash);
    if (deps.empty())
    {
        if (auto d = scene_.detail_store().get(root_hash))
            deps = d->deps;
    }
    if (!deps.empty())
        sel_dep_.seeded_from_detail = true;

    for (const std::string& v : deps)
    {
        if (v.empty())
            continue;
        if (static_cast<int>(sel_dep_.edges.size()) >= kMaxSelDepEdges)
            break;
        sel_dep_.edges.push_back(SelectionDepEdge{ root_hash, v, /*depth=*/0 });
        if (sel_dep_.node_set.count(v) != 0)
            continue;
        if (static_cast<int>(sel_dep_.nodes.size()) >= kMaxSelDepNodes)
            continue;
        // Keep missing deps as edges for ghosts; only force-draw known cubes.
        sel_dep_.node_set.insert(v);
        sel_dep_.nodes.push_back(v);
    }

    // Click/select: full length immediately. Replay: leave birth unset so grow runs.
    if (!animate_grow)
    {
        const float now = now_sec_();
        const float past = now - (kSelDepGrowSec + 0.05f);
        for (const SelectionDepEdge& e : sel_dep_.edges)
        {
            const std::string key = std::string("s|") + e.from + '|' + e.to;
            ephemeral_birth_sec_[key] = past;
        }
    }
}

int ScenePresenter::chain_idx_for_hash_(const std::string& hash) const
{
    if (hash.empty())
        return 255;
    if (auto d = scene_.detail_store().get(hash))
        return static_cast<int>(d->chain_idx()) & 15;
    if (auto n = scene_.graph().get(hash))
    {
        if (n->lane < 16)
            return static_cast<int>(n->lane);
    }
    return 255; // unknown → sort last
}

std::vector<std::string> ScenePresenter::sorted_deps_(const std::string& node_hash) const
{
    std::vector<std::string> out;
    if (node_hash.empty())
        return out;
    auto d = scene_.detail_store().get(node_hash);
    if (!d || d->deps.empty())
        return out;
    struct Item
    {
        std::string hash;
        int         shard = 255;
    };
    std::vector<Item> items;
    items.reserve(d->deps.size());
    for (const std::string& dep : d->deps)
    {
        if (dep.empty())
            continue;
        items.push_back(Item{ dep, chain_idx_for_hash_(dep) });
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.shard != b.shard)
            return a.shard < b.shard;
        return a.hash < b.hash;
    });
    out.reserve(items.size());
    for (const Item& it : items)
        out.push_back(it.hash);
    return out;
}

// RULEBOOK (tip_dep only — selection/hover are separate and may re-grow):
// - Primary confirmed: solid dual-RGBA gradient listing→dep (white head if missing).
// - Unconfirmed tip: full length immediately; color cyan→main dual over cyan_to_main_sec.
// - Secondary (prior tip): solid→translucent main (alpha floor > 0).
// - No length grow/restart for tip keys; leave-active → Fading;
//   listing hard-removed → Dying; soft corridor eviction → quiet erase.
// - Barrier planes: never for open live k=0.
void ScenePresenter::tip_dep_tick_and_draw_(
    DebugDrawer& debug,
    const std::unordered_map<std::string, glm::vec3>& positions,
    const std::unordered_map<std::string, glm::vec3>& block_colors,
    const std::unordered_set<std::string>& live_nodes,
    const std::unordered_set<std::string>& soft_evicted,
    const std::unordered_set<std::string>& drawn_set,
    const std::unordered_set<std::string>& green_display,
    const std::unordered_set<std::string>& cyan_owners,
    const std::unordered_set<std::string>& unconfirmed_tips,
    const std::unordered_set<std::string>& frontier_domain,
    float tip_len, float tip_rad, float shaft_r, float clearance,
    const Frustum* frustum,
    const glm::vec3* camera_eye)
{
    const float now = now_sec_();
    const glm::vec4 kMissingWhite(1.f, 1.f, 1.f, kTipSolidAlpha);
    const glm::vec4 kUnc = kUnconfirmedArrowColor();

    struct ActiveEdge
    {
        std::string key;
        std::string listing;
        std::string dep;
        glm::vec3 from_pos{};
        glm::vec3 to_pos{};
        int stagger_i = 0;
        TipTier tier = TipTier::Primary;
        bool dep_drawn = false;
    };

    std::unordered_set<std::string> active_keys;
    active_keys.reserve(128);
    std::vector<ActiveEdge> active;
    active.reserve(128);

    auto placement_rgb = [&](const std::string& h) -> glm::vec3 {
        auto it = block_colors.find(h);
        if (it != block_colors.end())
            return it->second;
        return glm::vec3(0.7f, 0.7f, 0.75f);
    };

    auto add_edges_from = [&](const std::string& listing_hash, TipTier tier,
                              bool only_to_frontier_deps, int stagger_base) {
        if (listing_hash.empty() || live_nodes.count(listing_hash) == 0)
            return;
        if (drawn_set.count(listing_hash) == 0)
            return;
        auto d = scene_.detail_store().get(listing_hash);
        if (!d)
            return;
        auto listing_it = positions.find(listing_hash);
        if (listing_it == positions.end())
            return;

        int stagger_i = 0;
        for (const std::string& dep_hash : d->deps)
        {
            if (dep_hash.empty())
                continue;
            if (only_to_frontier_deps && frontier_domain.count(dep_hash) == 0)
                continue;
            // Prefer live+drawn deps for main dual color; allow missing → white tip.
            const bool dep_live = live_nodes.count(dep_hash) != 0;
            const bool dep_drawn = drawn_set.count(dep_hash) != 0;
            auto dep_it = positions.find(dep_hash);
            if (!dep_live || dep_it == positions.end())
            {
                // Missing: still show arrow toward a ghost offset if listing known.
                if (!dep_live)
                {
                    ActiveEdge e;
                    e.key = tip_edge_key(listing_hash, dep_hash);
                    e.listing = listing_hash;
                    e.dep = dep_hash;
                    e.from_pos = listing_it->second;
                    e.to_pos = listing_it->second + glm::vec3(0.f, 0.f, 2.f);
                    e.stagger_i = stagger_base + stagger_i++;
                    e.tier = tier;
                    e.dep_drawn = false;
                    active_keys.insert(e.key);
                    active.push_back(std::move(e));
                }
                continue;
            }
            if (!dep_drawn)
                continue;
            ActiveEdge e;
            e.key = tip_edge_key(listing_hash, dep_hash);
            e.listing = listing_hash;
            e.dep = dep_hash;
            e.from_pos = listing_it->second;
            e.to_pos = dep_it->second;
            e.stagger_i = stagger_base + stagger_i++;
            e.tier = tier;
            e.dep_drawn = true;
            active_keys.insert(e.key);
            active.push_back(std::move(e));
        }
    };

    int tip_stagger_base = 0;
    // Primary confirmed tips: full deps, solid main dual.
    for (const std::string& tip : green_display)
    {
        add_edges_from(tip, TipTier::Primary, /*only_to_frontier=*/false, tip_stagger_base);
        tip_stagger_base += 2;
    }
    // Unconfirmed tips: full deps, cyan→main.
    for (const std::string& tip : unconfirmed_tips)
    {
        if (green_display.count(tip))
            continue;
        add_edges_from(tip, TipTier::Unconfirmed, false, tip_stagger_base);
        tip_stagger_base += 2;
    }
    // Secondary: previous primary tips still live (translucent main); also cyan_owners
    // that are not already unconfirmed full tips.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        const std::string& prev = prev_primary_tip_[lane];
        if (prev.empty() || green_display.count(prev) || unconfirmed_tips.count(prev))
            continue;
        if (live_nodes.count(prev) == 0)
            continue;
        add_edges_from(prev, TipTier::Secondary, false, tip_stagger_base);
        tip_stagger_base += 2;
    }
    for (const std::string& owner : cyan_owners)
    {
        if (green_display.count(owner) || unconfirmed_tips.count(owner))
            continue;
        add_edges_from(owner, TipTier::Secondary, /*only_to_frontier=*/true, tip_stagger_base);
        tip_stagger_base += 2;
    }

    // After emit: remember primary tips so next advance keeps them as secondary.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        std::string primary;
        for (const std::string& tip : green_display)
        {
            auto n = scene_.graph().get(tip);
            if (n && static_cast<int>(n->lane) == lane)
            {
                primary = tip;
                break;
            }
        }
        if (!primary.empty())
            prev_primary_tip_[lane] = primary;
    }

    for (const ActiveEdge& e : active)
    {
        DepArrowAnim& anim = tip_dep_anims_[e.key];
        anim.from_pos = e.from_pos;
        anim.to_pos = e.to_pos;
        anim.has_pos = true;
        anim.tip_scale = 1.f;

        const glm::vec3 list_rgb = placement_rgb(e.listing);
        const glm::vec3 dep_rgb =
            e.dep_drawn ? placement_rgb(e.dep) : glm::vec3(1.f, 1.f, 1.f);
        const glm::vec4 main_shaft(list_rgb, kTipSolidAlpha);
        const glm::vec4 main_tip = e.dep_drawn ? glm::vec4(dep_rgb, kTipSolidAlpha)
                                               : kMissingWhite;

        // Tip deps: full length immediately (no grow animation / never restart).
        if (anim.birth_sec < 0.f)
        {
            anim.birth_sec = now;
            anim.phase = ArrowPhase::Held;
            anim.tier = e.tier;
            if (e.tier == TipTier::Unconfirmed)
            {
                anim.cyan_to_main_u = 0.f;
                anim.cyan_to_main_start = now;
                anim.shaft_rgba = kUnc;
                anim.tip_rgba = kUnc;
            }
            else
            {
                anim.cyan_to_main_u = 1.f;
                anim.cyan_to_main_start = -1.f;
                anim.shaft_rgba = main_shaft;
                anim.tip_rgba = main_tip;
            }
            anim.secondary_alpha_u = 0.f;
            if (e.tier == TipTier::Secondary)
                anim.secondary_fade_start = now;
        }
        else
        {
            // Never re-enter Growing for tip keys.
            if (anim.phase == ArrowPhase::Growing)
                anim.phase = ArrowPhase::Held;
            if (anim.phase == ArrowPhase::Dying || anim.phase == ArrowPhase::Fading)
            {
                anim.phase = ArrowPhase::Held;
                anim.fade_start_sec = 0.f;
            }

            if (anim.tier != e.tier)
            {
                if (e.tier == TipTier::Secondary && anim.tier == TipTier::Primary)
                {
                    anim.secondary_fade_start = now;
                    anim.secondary_alpha_u = 0.f;
                }
                if (e.tier == TipTier::Primary)
                {
                    anim.secondary_alpha_u = 0.f;
                    anim.secondary_fade_start = -1.f;
                    anim.cyan_to_main_u = 1.f;
                }
                if (e.tier == TipTier::Unconfirmed && anim.cyan_to_main_u >= 1.f)
                {
                    anim.cyan_to_main_u = 0.f;
                    anim.cyan_to_main_start = now;
                }
                anim.tier = e.tier;
            }
        }

        // Advance cyan→main for unconfirmed.
        if (anim.tier == TipTier::Unconfirmed && anim.cyan_to_main_u < 1.f)
        {
            const float t0 =
                anim.cyan_to_main_start >= 0.f ? anim.cyan_to_main_start : now;
            anim.cyan_to_main_u =
                std::clamp((now - t0) / kCyanToMainSec, 0.f, 1.f);
        }
        else if (anim.tier != TipTier::Unconfirmed)
            anim.cyan_to_main_u = 1.f;

        // Secondary solid→translucent floor.
        if (anim.tier == TipTier::Secondary)
        {
            if (anim.secondary_fade_start < 0.f)
                anim.secondary_fade_start = now;
            anim.secondary_alpha_u = std::clamp(
                (now - anim.secondary_fade_start) / kSecondaryAlphaSec, 0.f, 1.f);
        }

        // Compose dual RGBA from cyan blend + main targets.
        // Unconfirmed → unconfirmed: cyan both ends (never main dual).
        const bool dep_confirmed =
            e.dep_drawn && !e.dep.empty() && scene_.is_confirmed_locked(e.dep);
        glm::vec4 shaft;
        glm::vec4 tipc;
        if (anim.tier == TipTier::Unconfirmed && e.dep_drawn && !dep_confirmed)
        {
            shaft = kUnc;
            tipc = kUnc;
        }
        else
        {
            const float u = anim.cyan_to_main_u;
            shaft = glm::mix(kUnc, main_shaft, u);
            tipc = glm::mix(kUnc, main_tip, u);
        }
        float a_scale = 1.f;
        if (anim.tier == TipTier::Secondary)
        {
            a_scale = glm::mix(1.f, kTipSecondaryAlpha / kTipSolidAlpha, anim.secondary_alpha_u);
        }
        else if (anim.tier == TipTier::Primary)
            a_scale = 1.f;
        shaft.a = kTipSolidAlpha * a_scale;
        tipc.a = kTipSolidAlpha * a_scale;
        anim.shaft_rgba = shaft;
        anim.tip_rgba = tipc;
    }

    // Inactive: Fading if both live (replaced tip deps); Dying if listing gone.
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        const std::string& key = it->first;
        DepArrowAnim& anim = it->second;
        std::string listing, dep;
        if (!parse_tip_edge_key(key, listing, dep))
        {
            it = tip_dep_anims_.erase(it);
            continue;
        }

        if (anim.phase == ArrowPhase::Dying || anim.phase == ArrowPhase::Fading)
        {
            ++it;
            continue;
        }

        const bool listing_live = live_nodes.count(listing) != 0;
        if (!listing_live)
        {
            // Soft corridor/RAM eviction (disk may re-admit): quiet erase — not chain death.
            if (soft_evicted.count(listing) != 0)
            {
                it = tip_dep_anims_.erase(it);
                continue;
            }
            if (anim.has_pos)
            {
                anim.phase = ArrowPhase::Dying;
                anim.fade_start_sec = now;
                ++it;
                continue;
            }
            it = tip_dep_anims_.erase(it);
            continue;
        }

        if (active_keys.count(key) == 0)
        {
            // Replaced tip deps: fade out (not instant erase).
            anim.phase = ArrowPhase::Fading;
            anim.fade_start_sec = now;
            ++it;
            continue;
        }
        ++it;
    }

    // Draw with priority: Primary > Unconfirmed > Secondary > Growing > Fading.
    std::vector<DepArrowAnim*> draw_list;
    draw_list.reserve(tip_dep_anims_.size());
    for (auto& kv : tip_dep_anims_)
    {
        if (kv.second.has_pos)
            draw_list.push_back(&kv.second);
    }
    std::sort(draw_list.begin(), draw_list.end(), [](const DepArrowAnim* a, const DepArrowAnim* b) {
        auto score = [](const DepArrowAnim* x) {
            int s = 0;
            if (x->phase == ArrowPhase::Held)
                s += 100;
            else if (x->phase == ArrowPhase::Growing)
                s += 50;
            else if (x->phase == ArrowPhase::Fading)
                s += 10;
            if (x->tier == TipTier::Primary)
                s += 30;
            else if (x->tier == TipTier::Unconfirmed)
                s += 20;
            else
                s += 10;
            return s;
        };
        return score(a) > score(b);
    });

    uint32_t drawn = 0;
    for (DepArrowAnim* panim : draw_list)
    {
        if (drawn >= kMaxTipDepArrows)
            break;
        DepArrowAnim& anim = *panim;
        glm::vec3 from_inset, to_inset;
        if (!inset_segment(anim.from_pos, anim.to_pos, clearance, from_inset, to_inset))
            continue;

        // Tip deps: always full length (grow_u=1); only selection/hover re-grow.
        constexpr float grow_u = 1.f;
        glm::vec4 shaft = anim.shaft_rgba;
        glm::vec4 tipc = anim.tip_rgba;

        if (anim.phase == ArrowPhase::Growing)
            anim.phase = ArrowPhase::Held;

        if (anim.phase == ArrowPhase::Fading)
        {
            const float t =
                std::clamp((now - anim.fade_start_sec) / kTipReplaceFadeSec, 0.f, 1.f);
            shaft.a *= (1.f - t);
            tipc.a *= (1.f - t);
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            const float t = std::clamp((now - anim.fade_start_sec) / kDeathSec, 0.f, 1.f);
            const glm::vec4 death = kDeathArrowColor();
            shaft = death;
            tipc = death;
            shaft.a *= (1.f - t);
            tipc.a *= (1.f - t);
        }

        if (shaft.a < 0.01f && tipc.a < 0.01f &&
            (anim.phase == ArrowPhase::Fading || anim.phase == ArrowPhase::Dying))
            continue;

        // Frustum cull: skip mesh if segment AABB is outside the view (anim still ages).
        if (frustum)
        {
            const glm::vec3 mid = 0.5f * (from_inset + to_inset);
            const glm::vec3 half = 0.5f * glm::abs(to_inset - from_inset) + glm::vec3(1.f);
            if (!frustum->intersects_aabb(mid, half))
                continue;
        }

        // Distance LOD: fewer radial segments when far from camera.
        uint32_t radial = 4;
        if (camera_eye)
        {
            const glm::vec3 mid = 0.5f * (from_inset + to_inset);
            const float dist = glm::length(mid - *camera_eye);
            if (dist < 40.f)
                radial = 6;
            else if (dist > 120.f)
                radial = 3;
        }

        debug.add_arrow(from_inset, to_inset, shaft, tipc, tip_len * anim.tip_scale,
                        tip_rad * anim.tip_scale, shaft_r * anim.tip_scale, radial, grow_u);
        ++drawn;
    }

    // Erase finished fades/deaths.
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        DepArrowAnim& anim = it->second;
        if (anim.phase == ArrowPhase::Fading)
        {
            const float t =
                std::clamp((now - anim.fade_start_sec) / kTipReplaceFadeSec, 0.f, 1.f);
            if (t >= 1.f)
            {
                it = tip_dep_anims_.erase(it);
                continue;
            }
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            const float t = std::clamp((now - anim.fade_start_sec) / kDeathSec, 0.f, 1.f);
            if (t >= 1.f)
            {
                it = tip_dep_anims_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void ScenePresenter::draw_bfs_traces_(
    DebugDrawer& debug,
    const std::unordered_map<std::string, glm::vec3>& positions,
    const std::unordered_set<std::string>& drawn_set)
{
    // Confirm BFS is policy-only (adapter cooperative). Draw thin newest-edge trails only.
    BlockScene::BfsTraceSnap snaps[BlockScene::kBfsThreadCount]{};
    int n = 0;
    scene_.copy_bfs_traces_locked(snaps, &n);
    constexpr int kMaxSeg = BlockScene::kBfsThreadCount * 12; // newest-ish only
    constexpr size_t kNewestPerThread = 8;
    int drawn_seg = 0;
    for (int ti = 0; ti < n && drawn_seg < kMaxSeg; ++ti)
    {
        const auto& tr = snaps[ti];
        if (tr.active == 0 && tr.edge_from.empty())
            continue;
        const glm::vec4 col = bfs_thread_color(tr.thread_id >= 0 ? tr.thread_id : ti);
        const size_t ne = std::min(tr.edge_from.size(), tr.edge_to.size());
        const size_t start = (ne > kNewestPerThread) ? (ne - kNewestPerThread) : 0;
        for (size_t e = start; e < ne && drawn_seg < kMaxSeg; ++e)
        {
            const std::string& a = tr.edge_from[e];
            const std::string& b = tr.edge_to[e];
            if (a.empty() || b.empty())
                continue;
            if (drawn_set.count(a) == 0 || drawn_set.count(b) == 0)
                continue;
            auto ia = positions.find(a);
            auto ib = positions.find(b);
            if (ia == positions.end() || ib == positions.end())
                continue;
            const float age =
                (ne > 1) ? static_cast<float>(e - start) /
                               static_cast<float>(std::max<size_t>(1, ne - start - 1))
                         : 1.f;
            glm::vec4 c = col;
            c.a *= (0.25f + 0.55f * age);
            debug.add_line(ia->second, ib->second, c);
            ++drawn_seg;
        }
    }
}

void ScenePresenter::update_death_and_walk_(
    const std::unordered_set<std::string>& live_nodes,
    const std::unordered_map<std::string, glm::vec3>& positions,
    const std::unordered_set<std::string>& soft_evicted,
    float now)
{
    for (const std::string& h : prev_live_nodes_)
    {
        if (live_nodes.count(h) != 0)
            continue;
        // Soft eviction: no red cube death (will re-admit from disk).
        if (soft_evicted.count(h) != 0)
            continue;
        bool already = false;
        for (const DyingBlock& d : dying_blocks_)
        {
            if (d.hash == h)
            {
                already = true;
                break;
            }
        }
        if (already)
            continue;
        DyingBlock db;
        db.hash = h;
        db.birth_sec = now;
        auto pit = prev_positions_.find(h);
        if (pit != prev_positions_.end())
            db.pos = pit->second;
        else
            continue;
        dying_blocks_.push_back(std::move(db));
    }

    for (auto it = dying_blocks_.begin(); it != dying_blocks_.end(); )
    {
        if (now - it->birth_sec >= kDeathSec)
            it = dying_blocks_.erase(it);
        else
            ++it;
    }

    // Green-tip walk animation along chain-walk path.
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        auto path = scene_.frontier_walk_locked(static_cast<uint32_t>(lane));
        FrontierWalkAnim& w = walk_by_lane_[lane];
        if (path.size() >= 2)
        {
            if (w.path != path)
            {
                w.path = std::move(path);
                w.index = 0;
                w.step_start_sec = now;
            }
            scene_.clear_frontier_walk_locked(static_cast<uint32_t>(lane));
        }
        if (w.path.empty())
            continue;
        if (w.step_start_sec < 0.f)
            w.step_start_sec = now;
        while (w.index + 1 < w.path.size() &&
               (now - w.step_start_sec) >= kWalkStepSec)
        {
            ++w.index;
            w.step_start_sec = now;
        }
        if (w.index + 1 >= w.path.size() && (now - w.step_start_sec) >= kWalkStepSec)
            w.path.clear();
    }

    prev_live_nodes_ = live_nodes;
    prev_positions_ = positions;
}

void ScenePresenter::prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                             DebugDrawer* debug)
{
    out = FrameSourceOutput{};

    std::unique_lock<std::mutex> lock(scene_.mutex());

    // Sticky session origin: set once. Never recompute from min block ts — that
    // jumps live_z / attached camera every time older history admits.
    int64_t timeline_origin_ms = scene_.timeline_origin_ms();
    if (timeline_origin_ms <= 0)
    {
        timeline_origin_ms = static_cast<int64_t>(std::time(nullptr)) * 1000 -
                             static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
        scene_.set_timeline_origin_ms(timeline_origin_ms);
    }

    const int64_t window_ms =
        static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    const int64_t genesis_ms = scene_.genesis_ms() > 0
                                   ? scene_.genesis_ms()
                                   : ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
    const uint64_t graph_gen = scene_.graph_generation();

    // Rebuild layout only when graph or timeline mapping changes (camera-only frames reuse).
    if (layout_cache_.graph_gen != graph_gen || layout_cache_.origin_ms != timeline_origin_ms ||
        layout_cache_.genesis_ms != genesis_ms || layout_cache_.window_ms != window_ms)
    {
        const std::vector<GraphNode> graph_nodes = scene_.nodes_snapshot_unsorted();
        LayoutParams layout_params;
        layout_params.meters_per_second = meters_per_second;
        layout_params.base_radius = kLayoutBaseRadius;
        layout_params.lane_count = 16;
        layout_params.timeline_origin_ms = timeline_origin_ms;

        layout_cache_.layout = layout_.build(graph_nodes, layout_params);
        layout_cache_.live_nodes.clear();
        layout_cache_.live_nodes.reserve(graph_nodes.size());
        layout_cache_.by_g.clear();
        layout_cache_.by_hash.clear();
        layout_cache_.by_hash.reserve(layout_cache_.layout.placements.size() * 2 + 1);

        auto g_of = [&](int64_t ts_ms) -> int {
            if (window_ms <= 0 || ts_ms <= genesis_ms)
                return 0;
            return static_cast<int>((ts_ms - genesis_ms) / window_ms);
        };
        for (size_t i = 0; i < layout_cache_.layout.placements.size(); ++i)
        {
            const PlacedBlock& p = layout_cache_.layout.placements[i];
            layout_cache_.live_nodes.insert(p.hash);
            layout_cache_.by_hash[p.hash] = i;
            layout_cache_.by_g[g_of(p.timestamp_ms)].push_back(i);
        }
        layout_cache_.graph_gen = graph_gen;
        layout_cache_.origin_ms = timeline_origin_ms;
        layout_cache_.genesis_ms = genesis_ms;
        layout_cache_.window_ms = window_ms;
    }

    const LayoutResult& layout = layout_cache_.layout;
    const auto& block_positions = layout.positions;
    const std::unordered_set<std::string>& live_nodes = layout_cache_.live_nodes;

    // Network HUD (segments) — already under scene mutex.
    const BlockScene::NetworkHud hud = scene_.network_hud_locked();

    const float now = now_sec_();
    const std::unordered_set<std::string> soft_evicted = scene_.take_soft_evicted_locked();
    update_death_and_walk_(live_nodes, block_positions, soft_evicted, now);

    // ------------------------------------------------------------------
    // Classify once (BlockFlow visual model)
    //   frontier_domain â€” domain confirmed tip hashes H_c (stable for cyan)
    //   green_display   â€” walk-anim tip or domain frontier (Sobel + green arrows)
    //   cyan_owners     â€” unconfirmed height>H_c that deps a domain frontier tip
    //   incomplete      â€” missing live deps, excluding green/cyan
    //   solid           â€” confirmed âˆ§ complete
    // ------------------------------------------------------------------
    const std::vector<NodeId> frontier_ids = scene_.confirmed_frontier_ids_locked();
    std::unordered_set<std::string> frontier_domain;
    frontier_domain.reserve(frontier_ids.size() * 2 + 1);
    for (const NodeId& h : frontier_ids)
        if (!h.empty())
            frontier_domain.insert(h);

    std::unordered_set<std::string> green_display;
    green_display.reserve(16);
    for (int lane = 0; lane < BlockScene::kLaneCount; ++lane)
    {
        const FrontierWalkAnim& w = walk_by_lane_[lane];
        if (!w.path.empty() && w.index < w.path.size())
            green_display.insert(w.path[w.index]);
    }
    if (green_display.empty())
        green_display = frontier_domain;
    // Uncles must never be green tips.
    for (auto it = green_display.begin(); it != green_display.end(); )
    {
        if (scene_.is_uncle_locked(*it))
            it = green_display.erase(it);
        else
            ++it;
    }

    int hc_by_lane[BlockScene::kLaneCount];
    scene_.copy_confirmed_heights_locked(hc_by_lane);

    if (!in.selected_hash.empty())
    {
        auto lit = block_positions.find(in.selected_hash);
        if (lit != block_positions.end())
        {
            out.has_look_target = true;
            out.look_target_pos = lit->second;
        }
    }

    // Slightly higher on dark canvas so unconfirmed cubes remain readable.
    constexpr float kUnconfirmedAlpha = 0.48f;
    const float scale = kDefaultBlockScale;

    // ------------------------------------------------------------------
    // Client-side render ring: ALPH_RENDER_RING_SEGMENTS (7) windows centered
    // on cam_k (±ALPH_RENDER_RING_HALF). At live tip (cam_k=0) clamps to 0..6.
    // Does not wait for network HUD; fetch/load rings are adapter-owned.
    // ------------------------------------------------------------------
    constexpr int kRingSize = ALPH_RENDER_RING_SEGMENTS;
    constexpr int kRingHalf = ALPH_RENDER_RING_HALF;
    const float eye_z = scene_.camera_scroll_z();
    const float R_cull = kLayoutBaseRadius * 1.35f;
    float seg_width_z = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS) * meters_per_second;

    const int64_t now_ms =
        static_cast<int64_t>(std::time(nullptr)) * 1000;
    const int G_live =
        window_ms > 0 && now_ms > genesis_ms
            ? static_cast<int>((now_ms - genesis_ms) / window_ms)
            : 0;
    const float live_z =
        -static_cast<float>(now_ms - timeline_origin_ms) * 0.001f * meters_per_second;
    const float older_sec = eye_z - live_z;
    int cam_k = 0;
    if (older_sec >= 1.f && window_ms > 0)
        cam_k = static_cast<int>(older_sec / (static_cast<float>(window_ms) * 0.001f));
    cam_k = std::clamp(cam_k, 0, std::max(0, G_live));

    struct ClientSeg
    {
        int     k = 0;
        int64_t from_ms = 0;
        int64_t to_ms = 0;
        float   z_new = 0.f;
        float   z_old = 0.f;
    };
    ClientSeg client_ring[kRingSize]{};
    int n_client = 0;
    // Centered corridor: cam_k - half … cam_k + half (clamp to [0, G_live]).
    for (int d = -kRingHalf; d <= kRingHalf; ++d)
    {
        const int k = cam_k + d;
        if (k < 0 || k > G_live)
            continue;
        const int G = std::max(0, G_live - k);
        int64_t from_ms = genesis_ms + static_cast<int64_t>(G) * window_ms;
        int64_t to_ms = from_ms + window_ms;
        if (k == 0 && now_ms < to_ms)
            to_ms = std::max(from_ms + 1, now_ms);
        if (to_ms <= from_ms)
            to_ms = from_ms + 1;
        ClientSeg& cs = client_ring[n_client];
        cs.k = k;
        cs.from_ms = from_ms;
        cs.to_ms = to_ms;
        const float z0 = ts_to_z(from_ms, timeline_origin_ms, meters_per_second);
        const float z1 = ts_to_z(to_ms, timeline_origin_ms, meters_per_second);
        cs.z_new = std::min(z0, z1);
        cs.z_old = std::max(z0, z1);
        ++n_client;
    }
    if (n_client > 0)
        seg_width_z = std::max(1.f, client_ring[0].z_old - client_ring[0].z_new);

    // Alpha fade: enter/leave sliding window.
    const float now_fade = now_sec_();
    float dt_fade = 1.f / 60.f;
    if (last_seg_fade_sec_ >= 0.f)
        dt_fade = std::clamp(now_fade - last_seg_fade_sec_, 0.f, 0.1f);
    last_seg_fade_sec_ = now_fade;
    std::unordered_set<int> want_k;
    for (int i = 0; i < n_client; ++i)
        want_k.insert(client_ring[i].k);
    for (int k : want_k)
    {
        // Start partially visible so first frames show cubes (not blank until fade ends).
        if (seg_fade_alpha_.find(k) == seg_fade_alpha_.end())
            seg_fade_alpha_[k] = 0.45f;
    }
    for (auto it = seg_fade_alpha_.begin(); it != seg_fade_alpha_.end();)
    {
        const bool want = want_k.count(it->first) != 0;
        const float target = want ? 1.f : 0.f;
        const float rate = want ? (1.f / kSegFadeInSec) : (1.f / kSegFadeOutSec);
        if (it->second < target)
            it->second = std::min(target, it->second + dt_fade * rate);
        else if (it->second > target)
            it->second = std::max(target, it->second - dt_fade * rate);
        if (!want && it->second <= 0.01f)
            it = seg_fade_alpha_.erase(it);
        else
            ++it;
    }

    auto fade_for_k = [&](int k) -> float {
        auto it = seg_fade_alpha_.find(k);
        return it != seg_fade_alpha_.end() ? it->second : 0.f;
    };
    // Merge HUD ring windows (authoritative after load) with client tip-relative ring.
    struct TimeWin
    {
        int64_t from_ms = 0;
        int64_t to_ms = 0;
        int     k = -1;
    };
    TimeWin merged[16]{};
    int n_merged = 0;
    auto add_win = [&](int64_t from_ms, int64_t to_ms, int k) {
        if (from_ms <= 0 && to_ms <= 0)
            return;
        if (to_ms <= from_ms)
            return;
        if (n_merged >= 16)
            return;
        merged[n_merged].from_ms = from_ms;
        merged[n_merged].to_ms = to_ms;
        merged[n_merged].k = k;
        ++n_merged;
    };
    for (int i = 0; i < n_client; ++i)
        add_win(client_ring[i].from_ms, client_ring[i].to_ms, client_ring[i].k);
    // HUD only when still wanted or fading out — never resurrect left rings.
    const int n_hud = std::clamp(hud.segment_count, 0, BlockScene::kMaxTimeSegments);
    for (int i = 0; i < n_hud; ++i)
    {
        const auto& s = hud.segments[i];
        if (want_k.count(s.index) == 0 && seg_fade_alpha_.count(s.index) == 0)
            continue;
        add_win(s.from_ms, s.to_ms, s.index);
    }

    auto fade_for_ts = [&](int64_t ts_ms) -> float {
        if (ts_ms <= 0)
            return 1.f;
        if (n_merged <= 0 && n_client <= 0)
            return 1.f;
        float best = 0.f;
        for (int i = 0; i < n_merged; ++i)
        {
            if (ts_ms >= merged[i].from_ms && ts_ms < merged[i].to_ms)
            {
                const int k = merged[i].k;
                if (k < 0)
                {
                    best = std::max(best, 1.f);
                    continue;
                }
                // Use fade map only — never invent 0.45 for missing/erased keys.
                best = std::max(best, fade_for_k(k));
            }
        }
        // Leaving segments still fading out.
        for (const auto& kv : seg_fade_alpha_)
        {
            if (kv.second <= 0.01f)
                continue;
            const int k = kv.first;
            if (k > G_live)
                continue;
            const int64_t live_from =
                genesis_ms + static_cast<int64_t>(std::max(0, G_live - k)) * window_ms;
            int64_t from_ms = live_from;
            int64_t to_ms = from_ms + window_ms;
            if (k == 0)
                to_ms = std::max(from_ms + 1, now_ms);
            if (ts_ms >= from_ms && ts_ms < to_ms)
                best = std::max(best, kv.second);
        }
        return best;
    };

    float vis_z_lo = 0.f;
    float vis_z_hi = 0.f;
    bool  have_vis_z = false;
    for (int i = 0; i < n_client; ++i)
    {
        if (fade_for_k(client_ring[i].k) < 0.02f)
            continue;
        if (!have_vis_z)
        {
            vis_z_lo = client_ring[i].z_new;
            vis_z_hi = client_ring[i].z_old;
            have_vis_z = true;
        }
        else
        {
            vis_z_lo = std::min(vis_z_lo, client_ring[i].z_new);
            vis_z_hi = std::max(vis_z_hi, client_ring[i].z_old);
        }
    }
    // Small pad for boundary jitter only — not a second full-segment draw range.
    const float ring_z_pad = std::max(8.f, seg_width_z * 0.02f);

    // G_seg set for client ring + fading lookbacks — role/instance work stays O(ring).
    std::unordered_set<int> active_g;
    active_g.reserve(16);
    for (int i = 0; i < n_client; ++i)
        active_g.insert(std::max(0, G_live - client_ring[i].k));
    for (const auto& kv : seg_fade_alpha_)
    {
        if (kv.second <= 0.02f)
            continue;
        if (kv.first > G_live)
            continue;
        active_g.insert(std::max(0, G_live - kv.first));
    }
    std::vector<size_t> ring_indices;
    ring_indices.reserve(4096);
    for (int G : active_g)
    {
        auto it = layout_cache_.by_g.find(G);
        if (it == layout_cache_.by_g.end())
            continue;
        ring_indices.insert(ring_indices.end(), it->second.begin(), it->second.end());
    }

    // Role classification only over ring placements (not full graph N).
    std::unordered_set<std::string> cyan_owners;
    cyan_owners.reserve(32);
    std::unordered_map<std::string, bool> missing_dep;
    missing_dep.reserve(ring_indices.size() + 32);
    std::vector<std::string> incomplete_pool;
    incomplete_pool.reserve(64);
    for (size_t idx : ring_indices)
    {
        if (idx >= layout.placements.size())
            continue;
        const PlacedBlock& p = layout.placements[idx];
        if (p.hash.empty())
            continue;
        if (scene_.is_confirmed_locked(p.hash) || scene_.is_uncle_locked(p.hash) ||
            p.is_uncle)
        {
            // Still need missing_dep for solids on confirmed ring blocks.
        }
        else if (p.lane < static_cast<uint8_t>(BlockScene::kLaneCount))
        {
            const int hc = hc_by_lane[p.lane];
            if (hc >= 0 && p.height > hc)
            {
                bool refs_frontier = false;
                scene_.detail_store().visit(p.hash, [&](const AlphBlock& d) {
                    for (const std::string& dep : d.deps)
                    {
                        if (!dep.empty() && frontier_domain.count(dep) != 0)
                        {
                            refs_frontier = true;
                            break;
                        }
                    }
                });
                if (refs_frontier)
                    cyan_owners.insert(p.hash);
            }
        }

        bool missing = false;
        scene_.detail_store().visit(p.hash, [&](const AlphBlock& d) {
            for (const std::string& dep : d.deps)
            {
                if (dep.empty())
                    continue;
                if (live_nodes.count(dep) == 0)
                {
                    missing = true;
                    break;
                }
            }
        });
        missing_dep[p.hash] = missing;
        if (missing && green_display.count(p.hash) == 0 && cyan_owners.count(p.hash) == 0)
            incomplete_pool.push_back(p.hash);
    }

    auto is_solid = [&](const std::string& h) -> bool {
        if (!scene_.is_confirmed_locked(h))
            return false;
        auto it = missing_dep.find(h);
        // Off-ring confirmed: treat complete if not classified this frame.
        if (it == missing_dep.end())
            return true;
        return !it->second;
    };

    auto block_in_visible_segment = [&](int64_t ts_ms) -> bool {
        if (n_merged <= 0 && n_client <= 0)
            return true;
        if (ts_ms <= 0)
            return true;
        // Primary gate: fade/time membership (no ghost neighbor segments).
        if (fade_for_ts(ts_ms) > 0.02f)
            return true;
        // Tiny Z pad only if already in a fading/wanted window time band via fade map miss
        // with client bounds (boundary jitter). Do not expand to off-ring history.
        if (have_vis_z && ts_ms > 0)
        {
            const float z = ts_to_z(ts_ms, timeline_origin_ms, meters_per_second);
            if (z >= vis_z_lo - ring_z_pad && z <= vis_z_hi + ring_z_pad)
            {
                // Accept only if timestamp falls in a client_ring ms window.
                for (int i = 0; i < n_client; ++i)
                {
                    if (ts_ms >= client_ring[i].from_ms && ts_ms < client_ring[i].to_ms)
                        return fade_for_k(client_ring[i].k) > 0.02f;
                }
            }
        }
        return false;
    };

    auto passes_txn_filter = [&](const PlacedBlock& placed) -> bool {
        if (!in.filter_txn_gt_1)
            return true;
        // Strict: only known multi-tx blocks (unknown -1 is hidden).
        return placed.txn_count > 1;
    };

    auto passes_amount_filter = [&](const PlacedBlock& placed) -> bool {
        if (in.filter_min_alph_atto.empty() || in.filter_min_alph_atto == "0")
            return true;
        // Unknown amount (empty) hidden when filter active.
        if (placed.alph_out_atto.empty())
            return false;
        return alph_cmp_atto(placed.alph_out_atto, in.filter_min_alph_atto) >= 0;
    };

    auto passes_unconfirmed_filter = [&](const PlacedBlock& placed) -> bool {
        if (!in.filter_unconfirmed_only)
            return true;
        // Hide main-chain confirmed; uncles / unconfirmed stay.
        if (scene_.is_confirmed_locked(placed.hash))
            return false;
        return true;
    };

    auto push_instance = [&](const PlacedBlock& placed, bool force) {
        if (out.instances.size() >= kMaxInstances)
            return false;
        // Cheap Z-slab reject before frustum (ring pad already tight).
        if (!force && have_vis_z)
        {
            if (placed.pos.z < vis_z_lo - ring_z_pad || placed.pos.z > vis_z_hi + ring_z_pad)
                return false;
        }
        // Lateral distance cull when free-looking away from the ring.
        if (!force)
        {
            const float lat2 = placed.pos.x * placed.pos.x + placed.pos.y * placed.pos.y;
            const float rmax = R_cull + 8.f;
            if (lat2 > rmax * rmax)
                return false;
        }
        const glm::vec3 half = in.instance_half_extents * scale;
        if (!force && in.frustum && !in.frustum->intersects_aabb(placed.pos, half))
            return false;

        glm::vec3 color = placed.color;
        if (!in.selected_hash.empty() && placed.hash == in.selected_hash)
            color = glm::mix(color, glm::vec3(1.f, 1.f, 1.f), 0.45f);
        else if (!in.hovered_hash.empty() && placed.hash == in.hovered_hash)
            color = glm::mix(color, glm::vec3(1.f, 0.9f, 0.4f), 0.35f);

        float alpha = is_solid(placed.hash) ? 1.0f : kUnconfirmedAlpha;
        if (placed.is_uncle || scene_.is_uncle_locked(placed.hash))
            alpha = std::min(alpha, 0.55f); // uncles always slightly translucent
        float seg_a = fade_for_ts(placed.timestamp_ms);
        // No forced floor — off-ring / faded blocks stay hidden.
        if (seg_a < 0.02f && !force)
            return false;
        if (force && seg_a < 0.02f)
            seg_a = 1.f; // selection/walk always visible when forced
        alpha *= seg_a;
        if (alpha < 0.02f)
            return false;
        out.instances.push_back(GpuInstance{ placed.pos, scale, color, alpha });
        out.pick_map.push_back(placed.hash);
        return true;
    };

    // Selection first-order deps: instant on click; grow only on Replay.
    const uint64_t replay_gen = scene_.walk_replay_gen();
    const bool want_replay =
        replay_gen != last_walk_replay_gen_ && !in.selected_hash.empty();
    if (want_replay)
        last_walk_replay_gen_ = replay_gen;

    if (in.selected_hash.empty())
    {
        if (!sel_dep_.root.empty())
            clear_selection_deps_();
    }
    else if (want_replay)
    {
        rebuild_selection_dep_one_hop_(in.selected_hash, /*animate_grow=*/true);
    }
    else if (in.selected_hash != sel_dep_.root)
    {
        rebuild_selection_dep_one_hop_(in.selected_hash, /*animate_grow=*/false);
    }
    else if (!sel_dep_.seeded_from_detail)
    {
        if (auto d = scene_.detail_store().get(in.selected_hash))
        {
            if (!d->deps.empty())
                rebuild_selection_dep_one_hop_(in.selected_hash, /*animate_grow=*/false);
        }
    }

    std::unordered_set<std::string> walk_force;
    collect_selection_dep_force_(walk_force);

    out.instances.reserve(std::min(ring_indices.size() + 64, size_t(kMaxInstances)));
    out.pick_map.reserve(out.instances.capacity());

    std::unordered_set<std::string> drawn;
    drawn.reserve(ring_indices.size() + 32);
    for (size_t idx : ring_indices)
    {
        if (idx >= layout.placements.size())
            continue;
        const PlacedBlock& placed = layout.placements[idx];
        if (!block_in_visible_segment(placed.timestamp_ms))
            continue;
        if (!passes_txn_filter(placed))
            continue;
        if (!passes_amount_filter(placed))
            continue;
        if (!passes_unconfirmed_filter(placed))
            continue;
        if (push_instance(placed, /*force=*/false))
            drawn.insert(placed.hash);
    }

    // Force-draw highlight roles via hash map (no full placement scan).
    auto try_force_hash = [&](const std::string& h, bool always) {
        if (h.empty() || drawn.count(h))
            return;
        auto it = layout_cache_.by_hash.find(h);
        if (it == layout_cache_.by_hash.end())
            return;
        const PlacedBlock& placed = layout.placements[it->second];
        if (!always && !block_in_visible_segment(placed.timestamp_ms))
            return;
        if (!always && !passes_txn_filter(placed))
            return;
        // Min ALPH / unconfirmed-only apply to role force-draws too.
        // Selection / hover / dep-walk keep always=true and stay visible.
        if (!always && !passes_amount_filter(placed))
            return;
        if (!always && !passes_unconfirmed_filter(placed))
            return;
        if (push_instance(placed, /*force=*/true))
            drawn.insert(placed.hash);
    };
    try_force_hash(in.selected_hash, /*always=*/true);
    try_force_hash(in.hovered_hash, /*always=*/true);
    for (const std::string& h : walk_force)
        try_force_hash(h, /*always=*/true);
    for (const std::string& h : green_display)
        try_force_hash(h, /*always=*/false);
    for (const std::string& h : cyan_owners)
        try_force_hash(h, /*always=*/false);
    for (const std::string& h : incomplete_pool)
        try_force_hash(h, /*always=*/false);

    // Dynamic near/far from visible segment Z span (+ lateral pad).
    {
        constexpr float kHardFarCap = 20000.f;
        constexpr float kMinNear    = 0.5f;
        float near_z = Camera::kDefaultNearZ;
        float far_z  = Camera::kDefaultFarZ;
        if (have_vis_z)
        {
            const float z_dist =
                std::max(std::abs(vis_z_hi - eye_z), std::abs(vis_z_lo - eye_z));
            const float lat_pad = R_cull + 40.f;
            const float pad     = std::max(seg_width_z * 0.35f, 40.f);
            near_z = kMinNear;
            far_z  = std::min(kHardFarCap, std::max(near_z + 50.f, z_dist + lat_pad + pad));
        }
        else if (!layout.placements.empty())
        {
            // Fallback: span of all placements.
            float z_lo = layout.placements[0].pos.z;
            float z_hi = z_lo;
            for (const PlacedBlock& p : layout.placements)
            {
                z_lo = std::min(z_lo, p.pos.z);
                z_hi = std::max(z_hi, p.pos.z);
            }
            const float z_dist =
                std::max(std::abs(z_hi - eye_z), std::abs(z_lo - eye_z));
            near_z = kMinNear;
            far_z  = std::min(kHardFarCap, std::max(near_z + 50.f, z_dist + R_cull + 80.f));
        }
        out.has_clip_suggestion = true;
        out.suggested_near_z    = near_z;
        out.suggested_far_z     = far_z;
    }

    for (const DyingBlock& db : dying_blocks_)
    {
        if (out.instances.size() >= kMaxInstances)
            break;
        const float t = std::clamp((now - db.birth_sec) / kDeathSec, 0.f, 1.f);
        const float alpha = (1.f - t) * 0.9f;
        out.instances.push_back(
            GpuInstance{ db.pos, scale, glm::vec3(1.f, 0.12f, 0.1f), alpha });
        out.pick_map.push_back(db.hash);
    }

    // Sobel outlines: app assigns colors; graphics draws all in one pass.
    // Selection gold is exclusive; else orange then cyan then green (mutually de-duped).
    {
        std::unordered_map<std::string, uint32_t> hash_to_idx;
        hash_to_idx.reserve(out.pick_map.size());
        for (size_t i = 0; i < out.pick_map.size(); ++i)
            hash_to_idx.emplace(out.pick_map[i], static_cast<uint32_t>(i));

        auto push_outline = [&](const std::string& h, const glm::vec4& color) {
            if (h.empty())
                return;
            const auto it = hash_to_idx.find(h);
            if (it == hash_to_idx.end())
                return;
            out.sobel_outlines.push_back(SobelOutlineInstance{ it->second, color });
        };

        uint32_t selected_idx = ~0u;
        if (!in.selected_hash.empty())
        {
            const auto it = hash_to_idx.find(in.selected_hash);
            if (it != hash_to_idx.end())
                selected_idx = it->second;
        }

        if (selected_idx != ~0u)
        {
            out.sobel_outlines.push_back(
                SobelOutlineInstance{ selected_idx, sty().sobel_select() });
        }

        if (in.enable_role_outlines && selected_idx == ~0u)
        {
            std::unordered_set<std::string> claimed;
            claimed.reserve(green_display.size() + cyan_owners.size() + incomplete_pool.size());

            for (const std::string& h : green_display)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_tip());
            }
            for (const std::string& h : cyan_owners)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_frontier());
            }
            for (const std::string& h : incomplete_pool)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_incomplete());
            }
        }
        else if (in.enable_role_outlines)
        {
            // Selection exclusive gold still allow co-visible roles on other cubes.
            std::unordered_set<std::string> claimed;
            claimed.insert(in.selected_hash);
            for (const std::string& h : green_display)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_tip());
            }
            for (const std::string& h : cyan_owners)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_frontier());
            }
            for (const std::string& h : incomplete_pool)
            {
                if (h.empty() || !hash_to_idx.count(h) || !claimed.insert(h).second)
                    continue;
                push_outline(h, sty().sobel_incomplete());
            }
        }
    }

    if (debug)
    {
        const float tip_len = std::max(0.18f, meters_per_second * 0.08f * 8.f);
        const float tip_rad = std::max(0.06f, meters_per_second * 0.03f * 8.f);
        const float shaft_r = tip_rad * 0.4f;
        const float clearance = std::max(0.55f, meters_per_second * 0.12f * 8.f);
        constexpr uint32_t kDepRadialNear = 6;
        constexpr uint32_t kDepRadialFar  = 3;
        constexpr uint32_t kMaxHoverArrows = 1024;
        // Unconfirmed tip-dep listings only H_c+1 .. H_c+3 (payload tip surface).
        constexpr int kUnconfirmedDepth = 3;

        // Per-lane unconfirmed near tip (cyan→main). Not deep history behind H_c.
        std::unordered_set<std::string> unconfirmed_tips;
        {
            for (const PlacedBlock& p : layout.placements)
            {
                if (p.hash.empty() || drawn.count(p.hash) == 0)
                    continue;
                if (p.lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
                    continue;
                if (scene_.is_confirmed_locked(p.hash) || p.is_uncle ||
                    scene_.is_uncle_locked(p.hash))
                    continue;
                if (green_display.count(p.hash))
                    continue;
                const int hc = hc_by_lane[p.lane];
                const int h = p.height;
                if (h < 0 || hc < 0)
                    continue; // no H_c → fallback pass below
                // Only heights strictly above confirmed tip, within 3 deep.
                if (h <= hc || h > hc + kUnconfirmedDepth)
                    continue;
                unconfirmed_tips.insert(p.hash);
            }
            // Fallback when no H_c: one max-height unconfirmed per lane.
            if (unconfirmed_tips.empty())
            {
                int best_h[BlockScene::kLaneCount];
                std::string best_id[BlockScene::kLaneCount];
                for (int i = 0; i < BlockScene::kLaneCount; ++i)
                {
                    best_h[i] = -1;
                    best_id[i].clear();
                }
                for (const PlacedBlock& p : layout.placements)
                {
                    if (p.hash.empty() || drawn.count(p.hash) == 0)
                        continue;
                    if (p.lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
                        continue;
                    if (scene_.is_confirmed_locked(p.hash) || p.is_uncle ||
                        scene_.is_uncle_locked(p.hash))
                        continue;
                    if (green_display.count(p.hash))
                        continue;
                    if (hc_by_lane[p.lane] >= 0)
                        continue; // already handled above
                    if (p.height > best_h[p.lane])
                    {
                        best_h[p.lane] = p.height;
                        best_id[p.lane] = p.hash;
                    }
                }
                for (int i = 0; i < BlockScene::kLaneCount; ++i)
                    if (!best_id[i].empty())
                        unconfirmed_tips.insert(best_id[i]);
            }
        }

        std::unordered_map<std::string, glm::vec3> block_colors;
        block_colors.reserve(layout.placements.size());
        for (const PlacedBlock& p : layout.placements)
            block_colors[p.hash] = p.color;

        tip_dep_tick_and_draw_(*debug, block_positions, block_colors, live_nodes, soft_evicted,
                               drawn, green_display, cyan_owners, unconfirmed_tips,
                               frontier_domain, tip_len, tip_rad, shaft_r, clearance,
                               in.frustum,
                               in.has_camera_eye ? &in.camera_eye : nullptr);
        draw_bfs_traces_(*debug, block_positions, drawn);

        uint32_t arrow_count = 0;
        std::unordered_set<std::string> eph_seen;

        auto eph_key = [](char kind, const std::string& from, const std::string& to) {
            return std::string(1, kind) + '|' + from + '|' + to;
        };

        auto add_eph_arrow = [&](char kind, const std::string& from_hash,
                                 const std::string& to_hash,
                                 const glm::vec3& from, const glm::vec3& to,
                                 const glm::vec4& color, float tip_scale, int stagger_i) {
            // Hover / misc ephemeral (own small budget).
            if (drawn.count(from_hash) == 0 || drawn.count(to_hash) == 0)
                return;
            if (arrow_count >= kMaxHoverArrows)
                return;
            glm::vec3 from_inset, to_inset;
            if (!inset_segment(from, to, clearance, from_inset, to_inset))
                return;
            const std::string key = eph_key(kind, from_hash, to_hash);
            const float grow = ephemeral_grow_u_(key, kArrowStagger * static_cast<float>(stagger_i),
                                                 eph_seen);
            if (grow <= 0.f)
                return;
            if (in.frustum)
            {
                const glm::vec3 mid = 0.5f * (from_inset + to_inset);
                const glm::vec3 half = 0.5f * glm::abs(to_inset - from_inset) + glm::vec3(1.f);
                if (!in.frustum->intersects_aabb(mid, half))
                    return;
            }
            debug->add_arrow(from_inset, to_inset, color,
                             tip_len * tip_scale, tip_rad * tip_scale,
                             shaft_r * tip_scale, 4u, grow);
            ++arrow_count;
        };

        const glm::vec4 kMissingOutline(0.75f, 0.75f, 0.8f, 0.9f);
        const float ghost_half = 1.0f;

        // Placement colors for dep-hover recolor (inspector Deps row).
        std::unordered_map<std::string, glm::vec3> placement_color;
        placement_color.reserve(layout.placements.size());
        for (const PlacedBlock& p : layout.placements)
            placement_color[p.hash] = p.color;

        // First-order selection deps: instant on click; grow only after Replay.
        const bool any_ui_dep_hover = !in.ui_dep_hover_hash.empty();
        if (!sel_dep_.root.empty() && !sel_dep_.edges.empty())
        {
            uint32_t sel_arrow_count = 0;
            int edge_i = 0;
            for (const SelectionDepEdge& e : sel_dep_.edges)
            {
                if (static_cast<int>(sel_arrow_count) >= kMaxSelDepArrows)
                    break;
                auto fit = block_positions.find(e.from);
                auto tit = block_positions.find(e.to);
                if (fit == block_positions.end() || tit == block_positions.end())
                {
                    ++edge_i;
                    continue;
                }
                if (drawn.count(e.from) == 0 || drawn.count(e.to) == 0)
                {
                    ++edge_i;
                    continue;
                }

                const bool focus =
                    any_ui_dep_hover && e.to == in.ui_dep_hover_hash;
                glm::vec4 arrow_col = kSelectionArrowColor();
                if (focus)
                {
                    auto cit = placement_color.find(e.to);
                    const glm::vec3 c = (cit != placement_color.end())
                                            ? cit->second
                                            : glm::vec3(arrow_col);
                    arrow_col = glm::vec4(c, 1.f);
                }
                else if (any_ui_dep_hover)
                    arrow_col.a = 0.35f;

                glm::vec3 from_inset, to_inset;
                if (!inset_segment(fit->second, tit->second, clearance, from_inset, to_inset))
                {
                    ++edge_i;
                    continue;
                }

                const float delay =
                    static_cast<float>(edge_i) * kSelDepEdgeStagger;
                const std::string key = eph_key('s', e.from, e.to);
                const float grow =
                    ephemeral_grow_u_(key, delay, eph_seen, kSelDepGrowSec);
                ++edge_i;
                if (grow <= 0.f)
                    continue;
                if (in.frustum)
                {
                    const glm::vec3 mid = 0.5f * (from_inset + to_inset);
                    const glm::vec3 half =
                        0.5f * glm::abs(to_inset - from_inset) + glm::vec3(1.f);
                    if (!in.frustum->intersects_aabb(mid, half))
                        continue;
                }

                const float tip_scale = focus ? 1.28f : 1.15f;
                debug->add_arrow(from_inset, to_inset, arrow_col, tip_len * tip_scale,
                                 tip_rad * tip_scale, shaft_r * tip_scale, 4u,
                                 grow);
                ++sel_arrow_count;
            }
        }

        // 1-hop missing ghosts for direct deps of selection only (no deep ghosts).
        if (!in.selected_hash.empty() && drawn.count(in.selected_hash) != 0)
        {
            auto listing_it = block_positions.find(in.selected_hash);
            if (listing_it != block_positions.end())
            {
                const AlphBlock* root_blk = nullptr;
                std::optional<AlphBlock> root_opt;
                if (in.selected_detail.hash == in.selected_hash)
                    root_blk = &in.selected_detail;
                else
                {
                    root_opt = scene_.detail_store().get(in.selected_hash);
                    if (root_opt)
                        root_blk = &*root_opt;
                }
                if (root_blk)
                {
                    const int parent_lane = root_blk->chain_idx();
                    int missing_i = 0;
                    const std::vector<std::string> gold_deps = sorted_deps_(root_blk->hash);
                    const std::vector<std::string>& dep_list =
                        gold_deps.empty() ? root_blk->deps : gold_deps;
                    for (const std::string& dep_hash : dep_list)
                    {
                        if (dep_hash.empty())
                            continue;
                        auto dep_it = block_positions.find(dep_hash);
                        if (dep_it != block_positions.end() && drawn.count(dep_hash) != 0)
                            continue; // drawn edge already in BFS fan
                        if (dep_it != block_positions.end() && drawn.count(dep_hash) == 0)
                            continue; // filtered out

                        const float angle =
                            ((static_cast<float>(parent_lane) + 0.35f +
                              0.08f * static_cast<float>(missing_i)) /
                             16.0f) *
                            2.0f * 3.14159265f;
                        const float radius = 20.0f * 0.9f;
                        glm::vec3 ghost(
                            -radius * std::cos(angle),
                            radius * std::sin(angle),
                            listing_it->second.z +
                                meters_per_second *
                                    static_cast<float>(ALPH_TARGET_BLOCK_SECONDS));
                        const bool focus_missing =
                            any_ui_dep_hover && dep_hash == in.ui_dep_hover_hash;
                        const glm::vec4 ghost_col =
                            focus_missing ? glm::vec4(1.f, 0.55f, 0.2f, 0.95f)
                                          : kMissingOutline;
                        debug->add_wire_box(ghost, ghost_half, ghost_col);
                        debug->add_line(listing_it->second, ghost, ghost_col);
                        ++missing_i;
                    }
                }
            }
        }

        if (!in.hovered_hash.empty() && in.hovered_hash != in.selected_hash &&
            drawn.count(in.hovered_hash) != 0)
        {
            if (auto d = scene_.detail_store().get(in.hovered_hash))
            {
                auto listing_it = block_positions.find(d->hash);
                if (listing_it != block_positions.end())
                {
                    int stagger_i = 0;
                    for (const std::string& dep_hash : d->deps)
                    {
                        if (drawn.count(dep_hash) == 0)
                            continue;
                        auto dep_it = block_positions.find(dep_hash);
                        if (dep_it == block_positions.end())
                            continue;
                        add_eph_arrow('h', d->hash, dep_hash, listing_it->second, dep_it->second,
                                      kHoverArrowColor(), 1.05f * sty().arrow_tip_scale,
                                      stagger_i++);
                    }
                }
            }
        }

        for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end(); )
        {
            if (eph_seen.find(it->first) == eph_seen.end())
                it = ephemeral_birth_sec_.erase(it);
            else
                ++it;
        }

        // Barrier planes on closed segment boundaries out to the LOAD ring (15),
        // both directions. Cubes/arrows/Sobel stay on the RENDER ring (7) only.
        // Never for open live interior (k=0 / G=G_live tip edge).
        {
            constexpr int kPlaneHalf = ALPH_LOAD_RING_SEGMENTS / 2; // 7 → span 15
            const float plane_half = kLayoutBaseRadius * 8.f;
            const float cross_eps = std::max(0.5f, seg_width_z * 0.02f);
            float bold_z = 0.f;
            bool have_bold = false;
            // Bold the closed segment under the eye (render-ring membership).
            for (int i = 0; i < n_client; ++i)
            {
                if (client_ring[i].k <= 0)
                    continue;
                if (eye_z >= client_ring[i].z_new - cross_eps &&
                    eye_z <= client_ring[i].z_old + cross_eps)
                {
                    bold_z = client_ring[i].z_old;
                    have_bold = true;
                    break;
                }
            }
            for (int d = -kPlaneHalf; d <= kPlaneHalf; ++d)
            {
                const int k = cam_k + d;
                if (k <= 0 || k > G_live)
                    continue; // no open-live plane
                const int G = std::max(0, G_live - k);
                if (G >= G_live)
                    continue;
                const int64_t from_ms = genesis_ms + static_cast<int64_t>(G) * window_ms;
                const float z_edge =
                    ts_to_z(from_ms, timeline_origin_ms, meters_per_second);
                if (std::abs(z_edge - eye_z) < std::max(2.f, seg_width_z * 0.05f))
                    continue;
                const bool bold = have_bold && std::abs(z_edge - bold_z) < 0.25f;
                // Steady α for load-only planes; render-ring planes can use cube fade.
                const float in_render = fade_for_k(k);
                const float fa = (in_render > 0.02f) ? in_render : 0.07f;
                glm::vec4 col = bold ? kBarrierPlaneColor : kBarrierPlaneDim;
                col.a *= fa;
                // Frustum cull barrier planes (mesh only; pad reduces edge strobe).
                if (in.frustum)
                {
                    const glm::vec3 center(0.f, 0.f, z_edge);
                    const glm::vec3 half(plane_half * 1.15f, plane_half * 1.15f, 1.f);
                    if (!in.frustum->intersects_aabb(center, half))
                        continue;
                }
                debug->add_z_plane_quad(z_edge, plane_half, col);
            }
        }

        // Gray translucent volumes: at API queue start; fade when fulfilled.
        // Sticky fade + retire tombstone: HUD still lists fulfilled slabs ~800ms after
        // admit; without retire we would re-create anim and re-flash every fade period.
        {
            const float now = now_sec_();
            std::unordered_set<int64_t> seen;
            const int nslab = std::clamp(hud.pending_fill_slab_count, 0,
                                         BlockScene::NetworkHud::kMaxPendingFillSlabs);
            for (int i = 0; i < nslab; ++i)
            {
                const auto& s = hud.pending_fill_slabs[i];
                if (s.to_ms <= s.from_ms)
                    continue;
                seen.insert(s.from_ms);

                // True re-queue after permanent fade: only unfulfilled HUD reopens.
                if (fill_slab_retired_.count(s.from_ms) != 0)
                {
                    if (s.fulfilled != 0)
                        continue; // still lagging fulfilled in HUD — stay gone
                    fill_slab_retired_.erase(s.from_ms);
                }

                auto it = fill_slab_anims_.find(s.from_ms);
                if (it == fill_slab_anims_.end())
                {
                    FillSlabAnim a;
                    a.from_ms = s.from_ms;
                    a.to_ms = s.to_ms;
                    // First sight at full α. If already fulfilled, start fade immediately.
                    a.fade_start_sec = (s.fulfilled != 0) ? now : -1.f;
                    fill_slab_anims_[s.from_ms] = a;
                }
                else
                {
                    it->second.to_ms = s.to_ms;
                    // Only start fade when first told fulfilled; never un-fade while alive.
                    if (s.fulfilled != 0 && it->second.fade_start_sec < 0.f)
                        it->second.fade_start_sec = now;
                }
            }
            // Left HUD entirely: begin fade if still solid.
            for (auto it = fill_slab_anims_.begin(); it != fill_slab_anims_.end(); )
            {
                if (seen.count(it->first) == 0 && it->second.fade_start_sec < 0.f)
                    it->second.fade_start_sec = now;
                if (it->second.fade_start_sec >= 0.f &&
                    (now - it->second.fade_start_sec) >= kFillSlabFadeSec)
                {
                    fill_slab_retired_.insert(it->first);
                    // Soft cap so long sessions do not retain unbounded tombstones.
                    if (fill_slab_retired_.size() > 4096u)
                    {
                        // Drop an arbitrary older half (set has no order — full clear is fine).
                        fill_slab_retired_.clear();
                        fill_slab_retired_.insert(it->first);
                    }
                    it = fill_slab_anims_.erase(it);
                    continue;
                }
                ++it;
            }

            const float slab_half = kLayoutBaseRadius * 6.5f;
            // Draw newest in-flight first; cap full-α to HTTP inflight so stacked fades
            // do not read as "12 subsegments loading."
            std::vector<const FillSlabAnim*> ordered;
            ordered.reserve(fill_slab_anims_.size());
            for (const auto& kv : fill_slab_anims_)
                ordered.push_back(&kv.second);
            std::sort(ordered.begin(), ordered.end(),
                      [](const FillSlabAnim* a, const FillSlabAnim* b) {
                          return a->from_ms > b->from_ms;
                      });
            int full_alpha_drawn = 0;
            for (const FillSlabAnim* ap : ordered)
            {
                const FillSlabAnim& a = *ap;
                const bool fading = a.fade_start_sec >= 0.f;
                glm::vec4 col = fading ? kFillVolumeFading : kFillVolumeInflight;
                if (fading)
                {
                    const float t =
                        std::clamp((now - a.fade_start_sec) / kFillSlabFadeSec, 0.f, 1.f);
                    const float u = t * t * (3.f - 2.f * t);
                    col.a *= (1.f - u);
                }
                else
                {
                    if (full_alpha_drawn >= kMaxFullAlphaFillVolumes)
                        col.a *= 0.35f; // extra queued: dim, not "another full load"
                    else
                        ++full_alpha_drawn;
                }
                if (col.a < 0.008f)
                    continue;
                const float z0 = ts_to_z(a.from_ms, timeline_origin_ms, meters_per_second);
                const float z1 = ts_to_z(a.to_ms, timeline_origin_ms, meters_per_second);
                if (in.frustum)
                {
                    const float z_lo = std::min(z0, z1);
                    const float z_hi = std::max(z0, z1);
                    const glm::vec3 center(0.f, 0.f, 0.5f * (z_lo + z_hi));
                    const float pad = 1.15f;
                    const glm::vec3 half(slab_half * pad, slab_half * pad,
                                         std::max(0.5f, 0.5f * (z_hi - z_lo) * pad));
                    if (!in.frustum->intersects_aabb(center, half))
                        continue;
                }
                debug->add_z_slab(z0, z1, slab_half, col);
            }
        }
    }

    out.ui.total_blocks = scene_.total_blocks();
    out.ui.selected_hash = in.selected_hash;
    out.ui.selected_detail = in.selected_detail;
    scene_.get_trace_status_locked(&out.ui.trace_phase, &out.ui.trace_offset);

    // Hover billboard: content + world anchor (overlay fades / projects).
    // Policy A: suppress while hovered == selected (inspector owns the block).
    {
        auto& bb = out.ui.block_billboard;
        bb = UiSnapshot::BlockBillboardUi{};
        const bool hover_ok = !in.hovered_hash.empty() &&
                              (in.selected_hash.empty() || in.hovered_hash != in.selected_hash);
        if (hover_ok)
        {
            auto pit = block_positions.find(in.hovered_hash);
            if (pit != block_positions.end())
            {
                const glm::vec3 block_pos = pit->second;
                glm::vec3 eye = in.has_camera_eye
                                    ? in.camera_eye
                                    : glm::vec3(0.f, 0.f, scene_.camera_scroll_z());
                glm::vec3 to_cam = eye - block_pos;
                const float len = glm::length(to_cam);
                if (len > 1e-4f)
                    to_cam /= len;
                else
                    to_cam = glm::vec3(0.f, 0.f, 1.f);
                constexpr float kBillboardOffset = 2.5f;
                const glm::vec3 world = block_pos + to_cam * kBillboardOffset;

                bb.want_visible = true;
                std::snprintf(bb.hash, sizeof(bb.hash), "%s", in.hovered_hash.c_str());
                bb.world_pos[0] = world.x;
                bb.world_pos[1] = world.y;
                bb.world_pos[2] = world.z;

                // Prefer placement height/lane/txn_count/alph; fall back to detail store.
                int height = -1;
                int chain_from = -1;
                int chain_to = -1;
                int txn_count = -1;
                std::string alph_atto;
                bool is_uncle = scene_.is_uncle_locked(in.hovered_hash);
                for (const PlacedBlock& p : layout.placements)
                {
                    if (p.hash != in.hovered_hash)
                        continue;
                    height = p.height;
                    chain_from = static_cast<int>(p.lane / ALPH_NUM_GROUPS);
                    chain_to = static_cast<int>(p.lane % ALPH_NUM_GROUPS);
                    txn_count = p.txn_count;
                    alph_atto = p.alph_out_atto;
                    is_uncle = is_uncle || p.is_uncle;
                    break;
                }
                if (auto d = scene_.detail_store().get(in.hovered_hash))
                {
                    if (height < 0)
                        height = d->height;
                    if (chain_from < 0)
                    {
                        chain_from = d->chainFrom;
                        chain_to = d->chainTo;
                    }
                    // Prefer preserved parse-time count (survives slim); not txns.size().
                    if (d->txn_count >= 0)
                        txn_count = d->txn_count;
                    else if (txn_count < 0 && !d->txns.empty())
                        txn_count = static_cast<int>(d->txns.size());
                    if (alph_atto.empty() && !d->alph_out_atto.empty())
                        alph_atto = d->alph_out_atto;
                    else if (alph_atto.empty() && !d->txns.empty())
                        alph_atto = alph_sum_block_outputs(*d);
                }
                bb.height = height;
                bb.chain_from = chain_from;
                bb.chain_to = chain_to;
                bb.txn_count = txn_count;
                bb.is_uncle = is_uncle ? 1 : 0;
                if (!alph_atto.empty() && alph_atto != "0")
                {
                    const std::string disp = alph_atto_to_display(alph_atto);
                    std::snprintf(bb.alph_out, sizeof(bb.alph_out), "%s", disp.c_str());
                }
            }
        }
    }
    {
        const auto tips = scene_.tip_ids();
        out.ui.tip_count = static_cast<int>(tips.size());
        out.ui.confirmed_tip_count = static_cast<int>(frontier_ids.size());
        scene_.copy_confirmed_heights_locked(out.ui.confirmed_height_by_lane);
    }
    {
        // prepare already holds scene_.mutex() — do not call network_hud() (re-lock = deadlock).
        // hud was captured earlier under the same lock for culling/planes.
        out.ui.net_domain = hud.domain;
        out.ui.net_status = hud.status;
        std::snprintf(out.ui.net_base_url, sizeof(out.ui.net_base_url), "%s", hud.base_url);
        std::snprintf(out.ui.net_status_detail, sizeof(out.ui.net_status_detail), "%s",
                      hud.status_detail);
        out.ui.lookback_windows_done = hud.lookback_windows_done;
        out.ui.lookback_windows_need = hud.lookback_windows_need;
        out.ui.lanes_with_frontier = hud.lanes_with_frontier;
        out.ui.open_confirm_walks = hud.open_confirm_walks;
        out.ui.pending_dep_fills = hud.pending_dep_fills;
        out.ui.timeline_holes = hud.timeline_holes;
        for (int i = 0; i < 16; ++i)
            out.ui.tip_height_by_lane[i] = hud.tip_height_by_lane[i];
        out.ui.stats_api_is_main = hud.stats_api_is_main;
        out.ui.stats_fetch_admitted = hud.stats_fetch_admitted;
        out.ui.stats_removed = hud.stats_removed;
        out.ui.stats_seed_q = hud.stats_seed_q;
        out.ui.last_poll_ms = hud.last_poll_ms;
        out.ui.poll_interval_sec = hud.poll_interval_sec;
        out.ui.net_switching = hud.switching;
        out.ui.cache_pressure_level = hud.cache_pressure_level;
        out.ui.browse_mode = hud.browse_mode;
        out.ui.disk_cache_segments = hud.disk_cache_segments;
        out.ui.disk_cache_mb = hud.disk_cache_mb;
        out.ui.disk_cache_boot_blocks = hud.disk_cache_boot_blocks;
        std::snprintf(out.ui.disk_cache_path, sizeof(out.ui.disk_cache_path), "%s",
                      hud.disk_cache_path);
        std::snprintf(out.ui.disk_cache_last_event, sizeof(out.ui.disk_cache_last_event), "%s",
                      hud.disk_cache_last_event);
        out.ui.timeline_origin_ms = timeline_origin_ms;
        out.ui.genesis_ms =
            scene_.genesis_ms() > 0 ? scene_.genesis_ms() : ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
        out.ui.meters_per_second = meters_per_second;
        out.ui.segment_count =
            std::clamp(hud.segment_count, 0, UiSnapshot::kMaxTimeSegments);
        for (int i = 0; i < out.ui.segment_count; ++i)
        {
            const auto& s = hud.segments[i];
            auto& d = out.ui.segments[i];
            d.index = s.index;
            d.global_index = s.global_index;
            d.from_ms = s.from_ms;
            d.to_ms = s.to_ms;
            d.load_ratio = s.load_ratio;
            d.confirmed_full = s.confirmed_full;
            d.block_count = s.block_count;
            d.expected_blocks = s.expected_blocks;
        }
    }
    for (const RecentFeedItem& b : scene_.feed())
    {
        FeedEntry e;
        e.hash = b.hash;
        e.chainFrom = b.chainFrom;
        e.chainTo = b.chainTo;
        e.height = b.height;
        out.ui.feed.push_back(std::move(e));
    }
}
