#pragma once

// Runtime style / anim timing for BlockFlow viz (brand-aligned defaults).
// Loaded from resource/style_blockflow.json when present — avoids recompiling
// for palette and hop-speed polish. Not a full theming engine.
// See docs/brand/alephium_palette.md and docs/layers/app.md (non-goals).

#include <cjson/cJSON.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <glm/glm.hpp>

struct StyleBlockflow
{
    float walk_hop_grow_sec = 0.12f;
    float walk_slot_stagger_sec = 0.03f;
    float walk_die_fade_sec = 0.22f;
    float walk_arrived_hold_sec = 0.02f;
    float walk_sobel_fade_sec = 0.35f;
    float arrow_tip_scale = 0.92f;
    float arrow_shaft_scale = 0.88f;
    float sobel_intensity = 1.18f;
    float bfs_alpha_scale = 0.55f;

    // Block admit pop-in (visible ring only; see motion_easing.hpp).
    float block_pop_sec = 0.28f;           // duration 0 → overshoot → 1
    float block_pop_overshoot = 1.08f;     // peak scale multiplier
    float block_pop_max_concurrent = 48.f; // cap simultaneous pops (cold start)

    // RGBA (a = shaft / outline base; Sobel multiplies intensity separately).
    glm::vec4 select_gold{ 0.94f, 0.76f, 0.29f, 0.88f };
    glm::vec4 walk_trace{ 0.72f, 0.42f, 0.95f, 0.90f }; // multi-hop (≠ gold)
    glm::vec4 tip_green{ 0.24f, 0.86f, 0.52f, 0.86f };
    // Unconfirmed tip / frontier-child (high contrast vs green main + lane oranges).
    // Defaults match death_red; product role is unconfirmed, not only removal fade.
    glm::vec4 unconfirmed_red{ 1.00f, 0.12f, 0.10f, 0.92f };
    // Legacy key name in JSON (frontier_cyan); kept for back-compat load only.
    glm::vec4 frontier_cyan{ 1.00f, 0.12f, 0.10f, 0.92f };
    glm::vec4 incomplete_amber{ 1.00f, 0.54f, 0.12f, 0.88f };
    glm::vec4 death_red{ 1.00f, 0.12f, 0.10f, 0.92f };

    glm::vec4 sobel_select() const
    {
        return glm::vec4(select_gold.r, select_gold.g, select_gold.b, sobel_intensity);
    }
    glm::vec4 sobel_walk_trace(float strength = 1.f) const
    {
        return glm::vec4(walk_trace.r, walk_trace.g, walk_trace.b,
                         sobel_intensity * std::clamp(strength, 0.f, 1.f));
    }
    glm::vec4 sobel_tip() const
    {
        return glm::vec4(tip_green.r, tip_green.g, tip_green.b, sobel_intensity);
    }
    // Unconfirmed role Sobel (was cyan frontier).
    glm::vec4 sobel_frontier() const
    {
        return glm::vec4(unconfirmed_red.r, unconfirmed_red.g, unconfirmed_red.b, sobel_intensity);
    }
    glm::vec4 sobel_unconfirmed() const { return sobel_frontier(); }
    glm::vec4 sobel_incomplete() const
    {
        return glm::vec4(incomplete_amber.r, incomplete_amber.g, incomplete_amber.b,
                         sobel_intensity);
    }

    static StyleBlockflow& global()
    {
        static StyleBlockflow s;
        return s;
    }

    // Try paths in order; keep defaults if none load.
    void try_load()
    {
        const char* paths[] = {
            "resource/style_blockflow.json",
            "style_blockflow.json",
            "../resource/style_blockflow.json",
            "../../resource/style_blockflow.json",
            "../../../resource/style_blockflow.json",
            "../../../../resource/style_blockflow.json",
        };
        for (const char* p : paths)
        {
            if (load_file_(p))
            {
                std::printf("[style] loaded %s\n", p);
                return;
            }
        }
        std::printf("[style] using compiled defaults (no style_blockflow.json)\n");
    }

private:
    static bool read_vec4_(const cJSON* arr, glm::vec4& out)
    {
        if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 3)
            return false;
        const cJSON* r = cJSON_GetArrayItem(arr, 0);
        const cJSON* g = cJSON_GetArrayItem(arr, 1);
        const cJSON* b = cJSON_GetArrayItem(arr, 2);
        const cJSON* a = cJSON_GetArrayItem(arr, 3);
        if (!r || !g || !b)
            return false;
        out.r = static_cast<float>(r->valuedouble);
        out.g = static_cast<float>(g->valuedouble);
        out.b = static_cast<float>(b->valuedouble);
        out.a = (a && cJSON_IsNumber(a)) ? static_cast<float>(a->valuedouble) : 1.f;
        return true;
    }

    static void read_f_(const cJSON* root, const char* key, float& out)
    {
        const cJSON* it = cJSON_GetObjectItem(root, key);
        if (it && cJSON_IsNumber(it))
            out = static_cast<float>(it->valuedouble);
    }

    bool load_file_(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string body = ss.str();
        if (body.empty())
            return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        if (!root)
            return false;

        read_f_(root, "walk_hop_grow_sec", walk_hop_grow_sec);
        read_f_(root, "walk_slot_stagger_sec", walk_slot_stagger_sec);
        read_f_(root, "walk_die_fade_sec", walk_die_fade_sec);
        read_f_(root, "walk_arrived_hold_sec", walk_arrived_hold_sec);
        read_f_(root, "walk_sobel_fade_sec", walk_sobel_fade_sec);
        read_f_(root, "arrow_tip_scale", arrow_tip_scale);
        read_f_(root, "arrow_shaft_scale", arrow_shaft_scale);
        read_f_(root, "sobel_intensity", sobel_intensity);
        read_f_(root, "bfs_alpha_scale", bfs_alpha_scale);
        read_f_(root, "block_pop_sec", block_pop_sec);
        read_f_(root, "block_pop_overshoot", block_pop_overshoot);
        read_f_(root, "block_pop_max_concurrent", block_pop_max_concurrent);

        glm::vec4 v;
        if (read_vec4_(cJSON_GetObjectItem(root, "select_gold"), v))
            select_gold = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "walk_trace"), v))
            walk_trace = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "tip_green"), v))
            tip_green = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "death_red"), v))
            death_red = v;
        // Prefer unconfirmed_red; else death_red; else legacy frontier_cyan.
        if (read_vec4_(cJSON_GetObjectItem(root, "unconfirmed_red"), v))
            unconfirmed_red = v;
        else if (read_vec4_(cJSON_GetObjectItem(root, "death_red"), v))
            unconfirmed_red = v;
        else if (read_vec4_(cJSON_GetObjectItem(root, "frontier_cyan"), v))
        {
            frontier_cyan = v;
            unconfirmed_red = v;
        }
        if (read_vec4_(cJSON_GetObjectItem(root, "frontier_cyan"), v))
            frontier_cyan = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "incomplete_amber"), v))
            incomplete_amber = v;

        cJSON_Delete(root);
        return true;
    }
};
