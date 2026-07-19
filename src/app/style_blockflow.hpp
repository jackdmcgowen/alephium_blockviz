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
    float arrow_tip_scale = 0.92f;
    float arrow_shaft_scale = 0.88f;
    float sobel_intensity = 1.18f;
    float bfs_alpha_scale = 0.55f;

    // RGBA (a = shaft / outline base; Sobel multiplies intensity separately).
    glm::vec4 select_gold{ 0.94f, 0.76f, 0.29f, 0.88f };
    glm::vec4 tip_green{ 0.24f, 0.86f, 0.52f, 0.86f };
    glm::vec4 frontier_cyan{ 0.18f, 0.90f, 0.94f, 0.86f };
    glm::vec4 incomplete_amber{ 1.00f, 0.54f, 0.12f, 0.88f };
    glm::vec4 death_red{ 1.00f, 0.12f, 0.10f, 0.92f };

    glm::vec4 sobel_select() const
    {
        return glm::vec4(select_gold.r, select_gold.g, select_gold.b, sobel_intensity);
    }
    glm::vec4 sobel_tip() const
    {
        return glm::vec4(tip_green.r, tip_green.g, tip_green.b, sobel_intensity);
    }
    glm::vec4 sobel_frontier() const
    {
        return glm::vec4(frontier_cyan.r, frontier_cyan.g, frontier_cyan.b, sobel_intensity);
    }
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
        read_f_(root, "arrow_tip_scale", arrow_tip_scale);
        read_f_(root, "arrow_shaft_scale", arrow_shaft_scale);
        read_f_(root, "sobel_intensity", sobel_intensity);
        read_f_(root, "bfs_alpha_scale", bfs_alpha_scale);

        glm::vec4 v;
        if (read_vec4_(cJSON_GetObjectItem(root, "select_gold"), v))
            select_gold = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "tip_green"), v))
            tip_green = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "frontier_cyan"), v))
            frontier_cyan = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "incomplete_amber"), v))
            incomplete_amber = v;
        if (read_vec4_(cJSON_GetObjectItem(root, "death_red"), v))
            death_red = v;

        cJSON_Delete(root);
        return true;
    }
};
