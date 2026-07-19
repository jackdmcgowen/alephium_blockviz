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

#include <glm/glm.hpp>

namespace
{
// World units per second of block timestamp (matches camera scroll_z units).
// Keep spacing: do not change meters_per_second or base_radius.
float meters_per_second = 1.0f;
constexpr float kLayoutBaseRadius = 20.0f;

// Colors from StyleBlockflow::global() (brand tokens / style_blockflow.json).
inline const StyleBlockflow& sty() { return StyleBlockflow::global(); }
inline glm::vec4 kCyanArrowColor() { return sty().frontier_cyan; }
inline glm::vec4 kGreenArrowColor() { return sty().tip_green; }
inline glm::vec4 kDeathArrowColor() { return sty().death_red; }
inline glm::vec4 kSelectionArrowColor() { return sty().select_gold; }
inline glm::vec4 kHoverArrowColor()
{
    glm::vec4 c = sty().select_gold;
    c.a *= 0.45f;
    return c;
}
const glm::vec4 kBarrierPlaneColor(0.35f, 0.75f, 0.95f, 0.10f);

// BFS confirm rays: muted brand family (not high-chroma rainbow).
glm::vec4 bfs_thread_color(int thread_id)
{
    const StyleBlockflow& s = sty();
    const glm::vec4 palette[] = {
        s.frontier_cyan, s.tip_green, s.incomplete_amber, s.select_gold,
        s.frontier_cyan * 0.75f + glm::vec4(0.1f, 0.1f, 0.12f, 0.f),
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
                                        std::unordered_set<std::string>& seen)
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
    return std::clamp(age / kArrowGrowSec, 0.f, 1.f);
}

void ScenePresenter::clear_dep_walk_()
{
    walk_root_hash_.clear();
    walk_slots_.clear();
    walk_seeded_from_detail_ = false;
    // Drop sticky selection ephemeral arrows from prior root.
    for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end();)
    {
        if (!it->first.empty() && (it->first[0] == 's' || it->first[0] == 'w'))
            it = ephemeral_birth_sec_.erase(it);
        else
            ++it;
    }
    // Start TRACE Sobel fade if we had visited marks; else clear.
    if (!walk_visited_sobel_.empty())
    {
        walk_sobel_fading_ = true;
        walk_sobel_fade_start_sec_ = now_sec_();
    }
}

bool ScenePresenter::dep_walk_active_() const
{
    for (const DepWalkSlot& s : walk_slots_)
        if (s.state != WalkSlotState::Dead)
            return true;
    return false;
}

void ScenePresenter::collect_walk_force_hashes_(std::unordered_set<std::string>& out) const
{
    if (!walk_root_hash_.empty())
        out.insert(walk_root_hash_);
    for (const DepWalkSlot& s : walk_slots_)
    {
        if (s.state == WalkSlotState::Dead || s.state == WalkSlotState::Pending)
            continue;
        if (!s.from_hash.empty())
            out.insert(s.from_hash);
        if (!s.to_hash.empty() && !s.ghost_target)
            out.insert(s.to_hash);
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

std::string ScenePresenter::next_walk_dep_for_slot_(
    const std::string& node_hash, int slot,
    const std::unordered_set<std::string>& visited) const
{
    // Slot s always means the s-th entry in shard-sorted deps — never "first free".
    if (node_hash.empty() || slot < 0)
        return {};
    const std::vector<std::string> sorted = sorted_deps_(node_hash);
    if (slot >= static_cast<int>(sorted.size()))
        return {};
    const std::string& dep = sorted[static_cast<size_t>(slot)];
    if (dep.empty() || visited.count(dep) != 0)
        return {};
    return dep;
}

void ScenePresenter::restart_dep_walk_(
    const std::string& root_hash,
    const std::unordered_map<std::string, glm::vec3>& positions)
{
    // Soft clear without forcing sobel fade mid-restart.
    walk_slots_.clear();
    walk_seeded_from_detail_ = false;
    for (auto it = ephemeral_birth_sec_.begin(); it != ephemeral_birth_sec_.end();)
    {
        if (!it->first.empty() && (it->first[0] == 's' || it->first[0] == 'w'))
            it = ephemeral_birth_sec_.erase(it);
        else
            ++it;
    }
    walk_visited_sobel_.clear();
    walk_sobel_fading_ = false;
    walk_sobel_fade_start_sec_ = -1.f;

    if (root_hash.empty())
    {
        walk_root_hash_.clear();
        return;
    }
    walk_root_hash_ = root_hash;
    const float now = now_sec_();
    const StyleBlockflow& st = sty();
    const float stagger = st.walk_slot_stagger_sec > 0.f ? st.walk_slot_stagger_sec
                                                         : kWalkSlotStagger;

    // hop0 TRACE: H → shard-sorted deps[s]; slot s sticks for the whole depth.
    std::vector<std::string> dep_roots = sorted_deps_(root_hash);
    if (!dep_roots.empty())
        walk_seeded_from_detail_ = true;
    walk_visited_sobel_[root_hash] = 1.f;

    const int nslots = kWalkSlotCount;
    walk_slots_.resize(static_cast<size_t>(nslots));
    for (int i = 0; i < nslots; ++i)
    {
        DepWalkSlot& s = walk_slots_[static_cast<size_t>(i)];
        s = DepWalkSlot{};
        s.slot = i; // permanent: always the i-th shard-sorted dep chain
        s.delay = stagger * static_cast<float>(i);
        s.state = WalkSlotState::Pending;
        s.state_start_sec = now;
        s.from_hash = root_hash;
        s.visited.insert(root_hash);
        auto fit = positions.find(root_hash);
        if (fit != positions.end())
            s.from_pos = fit->second;

        if (i >= static_cast<int>(dep_roots.size()) || dep_roots[static_cast<size_t>(i)].empty())
            continue; // empty slot → die when Pending ends

        const std::string& dep0 = dep_roots[static_cast<size_t>(i)];
        s.to_hash = dep0;
        auto tit = positions.find(dep0);
        if (tit != positions.end())
        {
            s.to_pos = tit->second;
            s.has_pos = (fit != positions.end());
            s.ghost_target = false;
        }
        else if (fit != positions.end())
        {
            s.to_pos = fit->second + glm::vec3(0.f, 0.f, 8.f);
            s.has_pos = true;
            s.ghost_target = true;
            scene_.request_block_fetch_locked(dep0);
        }
    }
}

void ScenePresenter::tick_dep_walk_(
    float now, const std::unordered_map<std::string, glm::vec3>& positions)
{
    // Late detail: re-seed once when deps appear.
    if (!walk_root_hash_.empty() && !walk_seeded_from_detail_)
    {
        if (auto d = scene_.detail_store().get(walk_root_hash_))
        {
            if (!d->deps.empty())
                restart_dep_walk_(walk_root_hash_, positions);
        }
    }

    // Fade TRACE Sobel after all walks finished.
    if (walk_sobel_fading_)
    {
        const float fade_sec = sty().walk_sobel_fade_sec > 0.f ? sty().walk_sobel_fade_sec
                                                               : 0.35f;
        const float t =
            std::clamp((now - walk_sobel_fade_start_sec_) / fade_sec, 0.f, 1.f);
        for (auto& kv : walk_visited_sobel_)
            kv.second = 1.f - t;
        if (t >= 1.f)
        {
            walk_visited_sobel_.clear();
            walk_sobel_fading_ = false;
        }
    }

    for (DepWalkSlot& s : walk_slots_)
    {
        if (s.state == WalkSlotState::Dead)
            continue;

        if (!s.from_hash.empty())
        {
            auto it = positions.find(s.from_hash);
            if (it != positions.end())
                s.from_pos = it->second;
        }
        if (!s.ghost_target && !s.to_hash.empty())
        {
            auto it = positions.find(s.to_hash);
            if (it != positions.end())
            {
                s.to_pos = it->second;
                s.has_pos = true;
            }
        }

        if (s.state == WalkSlotState::Pending)
        {
            if (now - s.state_start_sec < s.delay)
                continue;
            if (s.to_hash.empty() || !s.has_pos)
            {
                s.state = WalkSlotState::Dying;
                s.state_start_sec = now;
                s.die_alpha = 1.f;
                s.grow = 1.f;
                if (!s.has_pos && !s.from_hash.empty())
                {
                    auto it = positions.find(s.from_hash);
                    if (it != positions.end())
                    {
                        s.from_pos = it->second;
                        s.to_pos = s.from_pos + glm::vec3(0.f, 0.f, 6.f);
                        s.has_pos = true;
                        s.ghost_target = true;
                    }
                }
                if (!s.to_hash.empty())
                    scene_.request_block_fetch_locked(s.to_hash);
                continue;
            }
            s.state = WalkSlotState::Flying;
            s.state_start_sec = now;
            s.grow = 0.f;
            walk_visited_sobel_[s.from_hash] = 1.f;
            continue;
        }

        if (s.state == WalkSlotState::Flying)
        {
            const float grow_sec = sty().walk_hop_grow_sec > 0.f ? sty().walk_hop_grow_sec
                                                                : kWalkHopGrowSec;
            const float age = now - s.state_start_sec;
            s.grow = std::clamp(age / grow_sec, 0.f, 1.f);
            if (s.grow < 1.f)
                continue;
            s.state = WalkSlotState::Arrived;
            s.state_start_sec = now;
            s.grow = 1.f;
            if (!s.ghost_target && !s.to_hash.empty())
                walk_visited_sobel_[s.to_hash] = 1.f;
            continue;
        }

        if (s.state == WalkSlotState::Arrived)
        {
            const float hold = sty().walk_arrived_hold_sec > 0.f ? sty().walk_arrived_hold_sec
                                                                : kWalkArrivedHoldSec;
            if (now - s.state_start_sec < hold)
                continue;
            if (s.ghost_target || s.hops_done + 1 >= kWalkMaxHops)
            {
                s.state = WalkSlotState::Dying;
                s.state_start_sec = now;
                s.die_alpha = 1.f;
                if (s.ghost_target && !s.to_hash.empty())
                    scene_.request_block_fetch_locked(s.to_hash);
                continue;
            }
            s.visited.insert(s.to_hash);
            s.hops_done += 1;
            // Sticky slot: always the s-th shard-sorted dep of the current tip.
            const std::string next =
                next_walk_dep_for_slot_(s.to_hash, s.slot, s.visited);
            if (next.empty())
            {
                s.state = WalkSlotState::Dying;
                s.state_start_sec = now;
                s.die_alpha = 1.f;
                continue;
            }
            s.from_hash = s.to_hash;
            s.from_pos = s.to_pos;
            s.to_hash = next;
            auto tit = positions.find(next);
            if (tit != positions.end())
            {
                s.to_pos = tit->second;
                s.has_pos = true;
                s.ghost_target = false;
            }
            else
            {
                s.to_pos = s.from_pos + glm::vec3(0.f, 0.f, 8.f);
                s.has_pos = true;
                s.ghost_target = true;
                scene_.request_block_fetch_locked(next);
            }
            s.state = WalkSlotState::Flying;
            s.state_start_sec = now;
            s.grow = 0.f;
            continue;
        }

        if (s.state == WalkSlotState::Dying)
        {
            const float die_sec = sty().walk_die_fade_sec > 0.f ? sty().walk_die_fade_sec
                                                               : kWalkDieFadeSec;
            const float t = std::clamp((now - s.state_start_sec) / die_sec, 0.f, 1.f);
            s.die_alpha = 1.f - t;
            s.grow = 1.f;
            if (t >= 1.f)
                s.state = WalkSlotState::Dead;
        }
    }

    // When all slots dead, begin TRACE Sobel fade (if not already).
    if (!walk_slots_.empty() && !dep_walk_active_() && !walk_sobel_fading_ &&
        !walk_visited_sobel_.empty())
    {
        walk_sobel_fading_ = true;
        walk_sobel_fade_start_sec_ = now;
    }
}

void ScenePresenter::draw_dep_walk_(DebugDrawer& debug, float tip_len, float tip_rad,
                                    float shaft_r, float clearance)
{
    const glm::vec4 live_col = sty().walk_trace;
    const glm::vec4 die_col = kDeathArrowColor();
    for (const DepWalkSlot& s : walk_slots_)
    {
        if (s.state == WalkSlotState::Dead || s.state == WalkSlotState::Pending)
            continue;
        if (!s.has_pos)
            continue;
        glm::vec3 from_inset, to_inset;
        if (!inset_segment(s.from_pos, s.to_pos, clearance, from_inset, to_inset))
            continue;
        float grow_u = s.grow;
        glm::vec4 col = live_col;
        if (s.state == WalkSlotState::Dying || s.ghost_target)
        {
            col = die_col;
            col.a = s.die_alpha * 0.95f;
            grow_u = 1.f;
        }
        else if (s.state == WalkSlotState::Flying)
            col.a = live_col.a;
        else
            col.a = live_col.a * 0.95f;
        if (col.a < 0.02f)
            continue;
        const float ts = sty().arrow_tip_scale;
        const float ss = sty().arrow_shaft_scale;
        debug.add_arrow(from_inset, to_inset, col, tip_len * 1.05f * ts, tip_rad * ts,
                        shaft_r * ss, 8, grow_u);
    }
}

void ScenePresenter::tip_dep_tick_and_draw_(
    DebugDrawer& debug,
    const std::unordered_map<std::string, glm::vec3>& positions,
    const std::unordered_set<std::string>& live_nodes,
    const std::unordered_set<std::string>& drawn_set,
    const std::unordered_set<std::string>& green_display,
    const std::unordered_set<std::string>& cyan_owners,
    const std::unordered_set<std::string>& frontier_domain,
    float tip_len, float tip_rad, float shaft_r, float clearance)
{
    const float now = now_sec_();
    constexpr uint32_t kDepRadial = 8;
    constexpr uint32_t kMaxDepArrows = 512;

    struct ActiveEdge
    {
        std::string key;
        glm::vec3 from_pos{};
        glm::vec3 to_pos{};
        int stagger_i = 0;
        bool green = false;
    };

    std::unordered_set<std::string> active_keys;
    active_keys.reserve(64);
    std::vector<ActiveEdge> active;
    active.reserve(64);

    auto add_edges_from = [&](const std::string& listing_hash, bool green,
                              bool only_to_frontier_deps, int stagger_base) {
        if (listing_hash.empty() || live_nodes.count(listing_hash) == 0)
            return;
        // Both ends must be drawn this frame (filters / segment cull).
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
            if (dep_hash.empty() || live_nodes.count(dep_hash) == 0)
                continue;
            if (drawn_set.count(dep_hash) == 0)
                continue;
            if (only_to_frontier_deps && frontier_domain.count(dep_hash) == 0)
                continue;
            auto dep_it = positions.find(dep_hash);
            if (dep_it == positions.end())
                continue;
            ActiveEdge e;
            e.key = tip_edge_key(listing_hash, dep_hash);
            e.from_pos = listing_it->second;
            e.to_pos = dep_it->second;
            e.stagger_i = stagger_base + stagger_i++;
            e.green = green;
            active_keys.insert(e.key);
            active.push_back(std::move(e));
        }
    };

    int tip_stagger_base = 0;
    // Green: display frontier tip â†’ all live blockDeps.
    for (const std::string& tip : green_display)
    {
        add_edges_from(tip, /*green=*/true, /*only_to_frontier_deps=*/false, tip_stagger_base);
        tip_stagger_base += 2;
    }
    // Cyan: frontier children â†’ only edges into domain frontier tips.
    for (const std::string& owner : cyan_owners)
    {
        add_edges_from(owner, /*green=*/false, /*only_to_frontier_deps=*/true, tip_stagger_base);
        tip_stagger_base += 2;
    }

    for (const ActiveEdge& e : active)
    {
        DepArrowAnim& anim = tip_dep_anims_[e.key];
        anim.from_pos = e.from_pos;
        anim.to_pos = e.to_pos;
        anim.has_pos = true;
        anim.base_alpha = e.green ? kGreenArrowColor().a : kCyanArrowColor().a;
        anim.tip_scale = 1.f;

        // Cyan â†’ green color blend when the same edge key changes role.
        if (anim.want_green != e.green)
        {
            const float cur = (anim.confirm_blend_start_sec < 0.f)
                                  ? anim.confirm_blend_t
                                  : glm::mix(anim.confirm_blend_from, anim.want_green ? 1.f : 0.f,
                                             std::clamp((now - anim.confirm_blend_start_sec) /
                                                            kConfirmBlendSec,
                                                        0.f, 1.f));
            anim.confirm_blend_from = cur;
            anim.confirm_blend_start_sec = now;
            anim.want_green = e.green;
        }

        if (anim.birth_sec < 0.f)
        {
            anim.birth_sec = now + kArrowStagger * static_cast<float>(e.stagger_i);
            anim.phase = ArrowPhase::Growing;
            anim.confirm_blend_t = e.green ? 1.f : 0.f;
            anim.confirm_blend_start_sec = -1.f;
            anim.want_green = e.green;
        }
        else if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age >= kArrowGrowSec)
                anim.phase = ArrowPhase::Held;
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            // Edge returned to active set while dying â€” revive.
            anim.phase = ArrowPhase::Held;
            anim.fade_start_sec = 0.f;
        }
    }

    // Inactive / removed: red death if listing gone; erase otherwise (no red for role leave).
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

        if (anim.phase == ArrowPhase::Dying)
        {
            ++it;
            continue;
        }

        const bool listing_live = live_nodes.count(listing) != 0;
        const bool dep_live = live_nodes.count(dep) != 0;

        if (!listing_live || !dep_live)
        {
            if (!listing_live && anim.has_pos)
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
            it = tip_dep_anims_.erase(it);
            continue;
        }
        ++it;
    }

    uint32_t drawn = 0;
    for (auto it = tip_dep_anims_.begin(); it != tip_dep_anims_.end(); )
    {
        if (drawn >= kMaxDepArrows)
            break;
        DepArrowAnim& anim = it->second;
        if (!anim.has_pos)
        {
            ++it;
            continue;
        }

        glm::vec3 from_inset, to_inset;
        if (!inset_segment(anim.from_pos, anim.to_pos, clearance, from_inset, to_inset))
        {
            ++it;
            continue;
        }

        float grow_u = 1.f;
        float alpha = anim.base_alpha;

        // Advance confirm blend toward target.
        float blend = anim.confirm_blend_t;
        if (anim.confirm_blend_start_sec >= 0.f)
        {
            const float u = std::clamp((now - anim.confirm_blend_start_sec) / kConfirmBlendSec,
                                       0.f, 1.f);
            blend = glm::mix(anim.confirm_blend_from, anim.want_green ? 1.f : 0.f, u);
            anim.confirm_blend_t = blend;
            if (u >= 1.f)
                anim.confirm_blend_start_sec = -1.f;
        }
        else
        {
            blend = anim.want_green ? 1.f : 0.f;
            anim.confirm_blend_t = blend;
        }

        glm::vec4 color = glm::mix(kCyanArrowColor(), kGreenArrowColor(), blend);

        if (anim.phase == ArrowPhase::Growing)
        {
            const float age = now - anim.birth_sec;
            if (age <= 0.f)
            {
                ++it;
                continue;
            }
            grow_u = std::clamp(age / kArrowGrowSec, 0.f, 1.f);
            if (grow_u >= 1.f)
                anim.phase = ArrowPhase::Held;
        }
        else if (anim.phase == ArrowPhase::Dying)
        {
            color = kDeathArrowColor();
            const float t = std::clamp((now - anim.fade_start_sec) / kDeathSec, 0.f, 1.f);
            alpha = anim.base_alpha * (1.f - t);
            if (t >= 1.f)
            {
                it = tip_dep_anims_.erase(it);
                continue;
            }
        }

        color.a = alpha;
        debug.add_arrow(from_inset, to_inset, color,
                        tip_len * anim.tip_scale, tip_rad * anim.tip_scale,
                        shaft_r * anim.tip_scale, kDepRadial, grow_u);
        ++drawn;
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
    float now)
{
    for (const std::string& h : prev_live_nodes_)
    {
        if (live_nodes.count(h) != 0)
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

    const std::vector<GraphNode> graph_nodes = scene_.nodes_snapshot();

    // Sticky session origin: set once. Never recompute from min block ts — that
    // jumps live_z / attached camera every time older history admits.
    int64_t timeline_origin_ms = scene_.timeline_origin_ms();
    if (timeline_origin_ms <= 0)
    {
        timeline_origin_ms = static_cast<int64_t>(std::time(nullptr)) * 1000 -
                             static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
        scene_.set_timeline_origin_ms(timeline_origin_ms);
    }

    // Spacing unchanged: base_radius=20, meters_per_second=1, 16 lanes.
    LayoutParams layout_params;
    layout_params.meters_per_second = meters_per_second;
    layout_params.base_radius = kLayoutBaseRadius;
    layout_params.lane_count = 16;
    layout_params.timeline_origin_ms = timeline_origin_ms;

    LayoutResult layout = layout_.build(graph_nodes, layout_params);
    const auto& block_positions = layout.positions;

    // Network HUD (segments) — already under scene mutex.
    const BlockScene::NetworkHud hud = scene_.network_hud_locked();

    std::unordered_set<std::string> live_nodes;
    live_nodes.reserve(graph_nodes.size());
    for (const GraphNode& n : graph_nodes)
        live_nodes.insert(n.id);

    const float now = now_sec_();
    update_death_and_walk_(live_nodes, block_positions, now);

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

    std::unordered_set<std::string> cyan_owners;
    cyan_owners.reserve(32);
    for (const GraphNode& n : graph_nodes)
    {
        if (n.id.empty() || live_nodes.count(n.id) == 0)
            continue;
        if (scene_.is_confirmed_locked(n.id) || scene_.is_uncle_locked(n.id) ||
            n.role == BlockRole::Uncle)
            continue;
        if (n.lane >= static_cast<uint32_t>(BlockScene::kLaneCount))
            continue;
        const int hc = hc_by_lane[n.lane];
        if (hc < 0 || n.height <= hc)
            continue;

        auto d = scene_.detail_store().get(n.id);
        if (!d)
            continue;
        bool refs_frontier = false;
        for (const std::string& dep : d->deps)
        {
            if (!dep.empty() && frontier_domain.count(dep) != 0)
            {
                refs_frontier = true;
                break;
            }
        }
        if (refs_frontier)
            cyan_owners.insert(n.id);
    }

    std::unordered_map<std::string, bool> missing_dep;
    missing_dep.reserve(graph_nodes.size());
    std::vector<std::string> incomplete_pool;
    incomplete_pool.reserve(64);
    for (const GraphNode& n : graph_nodes)
    {
        bool missing = false;
        if (auto d = scene_.detail_store().get(n.id))
        {
            for (const std::string& dep : d->deps)
            {
                if (dep.empty())
                    continue;
                if (live_nodes.count(dep) == 0)
                {
                    missing = true;
                    break;
                }
            }
        }
        missing_dep[n.id] = missing;
        if (missing && green_display.count(n.id) == 0 && cyan_owners.count(n.id) == 0)
            incomplete_pool.push_back(n.id);
    }

    auto is_solid = [&](const std::string& h) -> bool {
        if (!scene_.is_confirmed_locked(h))
            return false;
        auto it = missing_dep.find(h);
        return !(it != missing_dep.end() && it->second);
    };

    if (!in.selected_hash.empty())
    {
        auto lit = block_positions.find(in.selected_hash);
        if (lit != block_positions.end())
        {
            out.has_look_target = true;
            out.look_target_pos = lit->second;
        }
    }

    out.instances.reserve(layout.placements.size());
    out.pick_map.reserve(layout.placements.size());
    // Slightly higher on dark canvas so unconfirmed cubes remain readable.
    constexpr float kUnconfirmedAlpha = 0.48f;
    const float scale = kDefaultBlockScale;

    // ------------------------------------------------------------------
    // Client-side sliding triple-buffer (immediate on camera Z). Does not wait
    // for network HUD poll cadence. Network still only *fetches* the ring.
    // ------------------------------------------------------------------
    constexpr int kRingSize = 3;
    const float eye_z = scene_.camera_scroll_z();
    const float R_cull = kLayoutBaseRadius * 1.35f;
    const int64_t window_ms =
        static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
    float seg_width_z = static_cast<float>(ALPH_LOOKBACK_WINDOW_SECONDS) * meters_per_second;

    const int64_t now_ms =
        static_cast<int64_t>(std::time(nullptr)) * 1000;
    const int64_t genesis_ms = scene_.genesis_ms() > 0
                                   ? scene_.genesis_ms()
                                   : ALPH_GENESIS_TIMESTAMP_MS_FALLBACK;
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
    for (int d = 0; d < kRingSize; ++d)
    {
        const int k = cam_k + d;
        if (k > G_live)
            break;
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
    TimeWin merged[8]{};
    int n_merged = 0;
    auto add_win = [&](int64_t from_ms, int64_t to_ms, int k) {
        if (from_ms <= 0 && to_ms <= 0)
            return;
        if (to_ms <= from_ms)
            return;
        if (n_merged >= 8)
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
    (void)R_cull;

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

    auto push_instance = [&](const PlacedBlock& placed, bool force) {
        if (out.instances.size() >= kMaxInstances)
            return false;
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
        if (seg_a < 0.02f)
            return false;
        alpha *= seg_a;
        if (alpha < 0.02f)
            return false;
        out.instances.push_back(GpuInstance{ placed.pos, scale, color, alpha });
        out.pick_map.push_back(placed.hash);
        return true;
    };

    // Selection multi-hop dep walk: restart on select change or replay request.
    const uint64_t replay_gen = scene_.walk_replay_gen();
    const bool want_replay =
        replay_gen != last_walk_replay_gen_ && !in.selected_hash.empty();
    if (want_replay)
        last_walk_replay_gen_ = replay_gen;

    if (in.selected_hash != walk_root_hash_ || want_replay)
    {
        if (in.selected_hash.empty())
            clear_dep_walk_();
        else
            restart_dep_walk_(in.selected_hash, block_positions);
    }
    if (!walk_root_hash_.empty() || walk_sobel_fading_)
        tick_dep_walk_(now, block_positions);

    std::unordered_set<std::string> walk_force;
    collect_walk_force_hashes_(walk_force);
    for (const auto& kv : walk_visited_sobel_)
        if (kv.second > 0.02f)
            walk_force.insert(kv.first);
    // Force-draw direct deps so selection fan + walk roots stay visible under filters.
    if (!in.selected_hash.empty())
    {
        if (auto d = scene_.detail_store().get(in.selected_hash))
        {
            for (const std::string& dep : d->deps)
                if (!dep.empty())
                    walk_force.insert(dep);
        }
    }

    std::unordered_set<std::string> drawn;
    for (const PlacedBlock& placed : layout.placements)
    {
        if (!block_in_visible_segment(placed.timestamp_ms))
            continue;
        if (!passes_txn_filter(placed))
            continue;
        if (!passes_amount_filter(placed))
            continue;
        if (push_instance(placed, /*force=*/false))
            drawn.insert(placed.hash);
    }

    // Force-draw highlight roles so Sobel can resolve them — only if segment
    // is in the draw set (or selection/hover, always).
    for (const PlacedBlock& placed : layout.placements)
    {
        if (drawn.count(placed.hash))
            continue;
        const bool is_sel =
            (!in.selected_hash.empty() && placed.hash == in.selected_hash) ||
            (!in.hovered_hash.empty() && placed.hash == in.hovered_hash);
        const bool is_walk = walk_force.count(placed.hash) != 0;
        if (!is_sel && !is_walk && !block_in_visible_segment(placed.timestamp_ms))
            continue;
        // Multi-tx filter: still force selected/hovered/walk; other force roles must pass.
        if (!is_sel && !is_walk && !passes_txn_filter(placed))
            continue;
        const bool force = is_sel || is_walk || missing_dep[placed.hash] ||
                           green_display.count(placed.hash) ||
                           cyan_owners.count(placed.hash);
        if (!force)
            continue;
        if (push_instance(placed, /*force=*/true))
            drawn.insert(placed.hash);
    }

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

        // TRACE Sobel on walk-visited nodes (selected stays gold above).
        for (const auto& kv : walk_visited_sobel_)
        {
            if (kv.second < 0.02f || kv.first.empty())
                continue;
            if (!in.selected_hash.empty() && kv.first == in.selected_hash)
                continue;
            push_outline(kv.first, sty().sobel_walk_trace(kv.second));
        }

        if (in.enable_role_outlines && selected_idx == ~0u)
        {
            std::unordered_set<std::string> claimed;
            claimed.reserve(green_display.size() + cyan_owners.size() + incomplete_pool.size());
            for (const auto& kv : walk_visited_sobel_)
                if (kv.second > 0.02f)
                    claimed.insert(kv.first);

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
            for (const auto& kv : walk_visited_sobel_)
                if (kv.second > 0.02f)
                    claimed.insert(kv.first);
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
        constexpr uint32_t kDepRadial = 8;
        constexpr uint32_t kMaxDepArrows = 512;

        tip_dep_tick_and_draw_(*debug, block_positions, live_nodes, drawn, green_display,
                               cyan_owners, frontier_domain, tip_len, tip_rad, shaft_r,
                               clearance);
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
            // No arrows to/from filtered-out cubes (both ends must be drawn).
            if (drawn.count(from_hash) == 0 || drawn.count(to_hash) == 0)
                return;
            if (arrow_count >= kMaxDepArrows)
                return;
            glm::vec3 from_inset, to_inset;
            if (!inset_segment(from, to, clearance, from_inset, to_inset))
                return;
            const std::string key = eph_key(kind, from_hash, to_hash);
            const float grow = ephemeral_grow_u_(key, kArrowStagger * static_cast<float>(stagger_i),
                                                 eph_seen);
            if (grow <= 0.f)
                return;
            debug->add_arrow(from_inset, to_inset, color,
                             tip_len * tip_scale, tip_rad * tip_scale,
                             shaft_r * tip_scale, kDepRadial, grow);
            ++arrow_count;
        };

        const glm::vec4 kMissingOutline(0.75f, 0.75f, 0.8f, 0.9f);
        const float ghost_half = 1.0f;

        // Placement colors for dep-hover recolor (inspector Deps row).
        std::unordered_map<std::string, glm::vec3> placement_color;
        placement_color.reserve(layout.placements.size());
        for (const PlacedBlock& p : layout.placements)
            placement_color[p.hash] = p.color;

        auto draw_selection_deps = [&](const AlphBlock& block) {
            if (drawn.count(block.hash) == 0)
                return;
            auto listing_it = block_positions.find(block.hash);
            if (listing_it == block_positions.end())
                return;
            const glm::vec3& listing_pos = listing_it->second;
            const int parent_lane = block.chain_idx();
            int missing_i = 0;
            int stagger_i = 0;
            const bool any_ui_dep_hover = !in.ui_dep_hover_hash.empty();

            // Same shard-sorted order as TRACE walk slots.
            const std::vector<std::string> gold_deps = sorted_deps_(block.hash);
            const std::vector<std::string>& dep_list =
                gold_deps.empty() ? block.deps : gold_deps;
            for (const std::string& dep_hash : dep_list)
            {
                auto dep_it = block_positions.find(dep_hash);
                if (dep_it != block_positions.end() && drawn.count(dep_hash) != 0)
                {
                    const bool focus =
                        any_ui_dep_hover && dep_hash == in.ui_dep_hover_hash;
                    glm::vec4 arrow_col = kSelectionArrowColor();
                    if (focus)
                    {
                        auto cit = placement_color.find(dep_hash);
                        const glm::vec3 c = (cit != placement_color.end())
                                                ? cit->second
                                                : glm::vec3(arrow_col);
                        arrow_col = glm::vec4(c, 1.f);
                    }
                    else if (any_ui_dep_hover)
                    {
                        // Dim non-focused selection edges while inspecting one dep.
                        arrow_col.a = 0.35f;
                    }
                    add_eph_arrow('s', block.hash, dep_hash, listing_pos, dep_it->second,
                                  arrow_col, focus ? 1.28f : 1.15f, stagger_i++);
                    continue;
                }

                // Missing-dep ghost only if parent is drawn (selection always force-drawn).
                if (dep_it != block_positions.end() && drawn.count(dep_hash) == 0)
                    continue; // dep filtered out — no arrow/ghost

                const float angle =
                    ((static_cast<float>(parent_lane) + 0.35f +
                      0.08f * static_cast<float>(missing_i)) /
                     16.0f) *
                    2.0f * 3.14159265f;
                const float radius = 20.0f * 0.9f;
                // Match layout X flip (−cos) so ghost sits with lane ring.
                glm::vec3 ghost(
                    -radius * std::cos(angle),
                    radius * std::sin(angle),
                    listing_pos.z + meters_per_second *
                                        static_cast<float>(ALPH_TARGET_BLOCK_SECONDS));
                const bool focus_missing =
                    any_ui_dep_hover && dep_hash == in.ui_dep_hover_hash;
                const glm::vec4 ghost_col =
                    focus_missing ? glm::vec4(1.f, 0.55f, 0.2f, 0.95f) : kMissingOutline;
                debug->add_wire_box(ghost, ghost_half, ghost_col);
                debug->add_line(listing_pos, ghost, ghost_col);
                ++missing_i;
            }
        };

        // One-hop selection fan stays for whole selection; multi-hop continues from deps.
        if (!in.selected_hash.empty() && in.selected_detail.hash == in.selected_hash)
            draw_selection_deps(in.selected_detail);
        else if (!in.selected_hash.empty())
        {
            if (auto d = scene_.detail_store().get(in.selected_hash))
                draw_selection_deps(*d);
        }
        if (dep_walk_active_() && debug)
            draw_dep_walk_(*debug, tip_len, tip_rad, shaft_r, clearance);

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

        // Barrier planes from client ring (immediate on cam_k change) + fade alpha.
        if (n_client > 0)
        {
            const float plane_half = kLayoutBaseRadius * 8.f;
            const float cross_eps = std::max(0.5f, seg_width_z * 0.02f);
            float bold_z = 0.f;
            bool have_bold = false;
            for (int i = 0; i < n_client; ++i)
            {
                if (eye_z >= client_ring[i].z_new - cross_eps &&
                    eye_z <= client_ring[i].z_old + cross_eps)
                {
                    bold_z = client_ring[i].z_old;
                    have_bold = true;
                    break;
                }
            }
            // Fading-out segments still draw planes until alpha ~0.
            for (const auto& kv : seg_fade_alpha_)
            {
                if (kv.second < 0.02f)
                    continue;
                const int k = kv.first;
                if (k > G_live)
                    continue;
                const int G = std::max(0, G_live - k);
                const int64_t from_ms = genesis_ms + static_cast<int64_t>(G) * window_ms;
                const float past_z =
                    ts_to_z(from_ms, timeline_origin_ms, meters_per_second);
                // from is older edge for window (higher Z when genesis < now).
                const float z_edge = past_z; // past edge of segment k
                const bool bold = have_bold && std::abs(z_edge - bold_z) < 0.25f;
                glm::vec4 col = bold ? kBarrierPlaneColor : glm::vec4(0.35f, 0.75f, 0.95f, 0.06f);
                col.a *= kv.second;
                debug->add_z_plane_quad(z_edge, plane_half, col);
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
        out.ui.lookback_windows_done = hud.lookback_windows_done;
        out.ui.lookback_windows_need = hud.lookback_windows_need;
        out.ui.lanes_with_frontier = hud.lanes_with_frontier;
        out.ui.open_confirm_walks = hud.open_confirm_walks;
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
