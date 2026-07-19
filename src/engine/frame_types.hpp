#pragma once

// Frame-source types (host implements IFrameSource; graphics calls prepare).
// Split from engine.hpp so leaf TUs need not pull full IEngine surface.

#include "domain/alph_block.hpp"
#include "app/ui_snapshot.hpp"
#include "graphics/gpu_pub_lib.h"

#include <string>
#include <vector>

#include <glm/glm.hpp>

class DebugDrawer;
struct Frustum;

struct FrameSourceInput
{
    std::string selected_hash;
    std::string hovered_hash;
    // Inspector Deps row hover (not 3D pick); recolors selection→dep arrow.
    std::string ui_dep_hover_hash;
    AlphBlock   selected_detail;
    const Frustum* frustum = nullptr;
    glm::vec3 instance_half_extents{ 1.f, 1.f, 1.f };
    glm::vec3 camera_eye{ 0.f, 0.f, 0.f };
    bool      has_camera_eye = false;
    // When true, only draw placements with txn_count > 1 (selection/hover always drawn).
    bool      filter_txn_gt_1 = false;
    // Min block output ALPH (atto digit string); empty or "0" = off.
    std::string filter_min_alph_atto;
    // When false, presenter omits role outlines (tips/cyan/orange); selection gold still emitted.
    bool      enable_role_outlines = true;
};

struct FrameSourceOutput
{
    std::vector<GpuInstance> instances;
    std::vector<std::string> pick_map;
    bool      has_look_target = false;
    glm::vec3 look_target_pos{ 0.f };
    UiSnapshot ui{};

    // App-built Sobel list: instance_index into instances/pick_map + outline color.
    // Graphics draws all in one pass (white edge × instance color). No role names.
    std::vector<SobelOutlineInstance> sobel_outlines;

    // Dynamic camera clip from visible segment span (applied after prepare for UBO).
    bool  has_clip_suggestion = false;
    float suggested_near_z    = 1.f;
    float suggested_far_z     = 5000.f;
};

class IFrameSource
{
public:
    virtual ~IFrameSource() = default;
    virtual void prepare(const FrameSourceInput& in, FrameSourceOutput& out,
                         DebugDrawer* debug) = 0;
};
