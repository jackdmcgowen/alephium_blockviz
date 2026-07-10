#include "debug_drawer.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DebugDrawer::clear()
{
    verts_.clear();
    indices_.clear();
    line_verts_.clear();
}

void DebugDrawer::add_line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)
{
    line_verts_.push_back({ a, color });
    line_verts_.push_back({ b, color });
}

void DebugDrawer::add_wire_box(const glm::vec3& center, float half_extent, const glm::vec4& color)
{
    // 8 corners of AABB; 12 edges only (no face diagonals)
    const float s = half_extent;
    const glm::vec3 c = center;
    const glm::vec3 p[8] = {
        c + glm::vec3(-s, -s, -s),
        c + glm::vec3( s, -s, -s),
        c + glm::vec3( s,  s, -s),
        c + glm::vec3(-s,  s, -s),
        c + glm::vec3(-s, -s,  s),
        c + glm::vec3( s, -s,  s),
        c + glm::vec3( s,  s,  s),
        c + glm::vec3(-s,  s,  s),
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, // bottom
        {4,5},{5,6},{6,7},{7,4}, // top
        {0,4},{1,5},{2,6},{3,7}, // verticals
    };
    for (const auto& e : edges)
        add_line(p[e[0]], p[e[1]], color);
}

void DebugDrawer::append_tri(uint32_t i0, uint32_t i1, uint32_t i2)
{
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
}

void DebugDrawer::build_basis(const glm::vec3& forward, glm::vec3& right, glm::vec3& up)
{
    // Prefer world-up (Y) cross; fall back when nearly parallel (same idea as old arrow shader).
    glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, world_up)) > 0.999f)
        world_up = glm::vec3(1.0f, 0.0f, 0.0f);

    right = glm::normalize(glm::cross(world_up, forward));
    up    = glm::cross(forward, right);
}

void DebugDrawer::add_mesh(const DebugVertex* verts, uint32_t vert_count,
                           const uint32_t* indices, uint32_t index_count)
{
    if (!verts || !indices || vert_count == 0 || index_count == 0)
        return;

    const uint32_t base = vertex_count();
    verts_.insert(verts_.end(), verts, verts + vert_count);
    for (uint32_t i = 0; i < index_count; ++i)
        indices_.push_back(base + indices[i]);
}

void DebugDrawer::add_arrow(const glm::vec3& start,
                            const glm::vec3& end,
                            const glm::vec4& color,
                            float tip_length,
                            float tip_radius,
                            float shaft_radius,
                            uint32_t radial_segments)
{
    const glm::vec3 dir = end - start;
    const float total = glm::length(dir);
    if (total < 1e-4f)
        return;

    if (radial_segments < 3)
        radial_segments = 3;

    if (shaft_radius < 0.0f)
        shaft_radius = tip_radius * kDefaultShaftToTip;

    if (tip_length < 0.0f)
        tip_length = 0.0f;
    if (tip_radius < 0.0f)
        tip_radius = 0.0f;
    if (shaft_radius < 0.0f)
        shaft_radius = 0.0f;

    float tip_len = std::min(tip_length, total * kMaxTipFraction);
    if (tip_len < 1e-5f && total > 1e-4f)
        tip_len = std::min(total * 0.25f, tip_length > 0.0f ? tip_length : total * 0.25f);

    const float shaft_len = std::max(0.0f, total - tip_len);
    const glm::vec3 forward = dir / total;

    glm::vec3 right, up;
    build_basis(forward, right, up);

    const uint32_t N = radial_segments;
    // Layout: shaft base ring (N), shaft top ring (N), tip base ring (N), apex (1), butt center (1)
    const uint32_t base = vertex_count();

    const glm::vec3 shaft_top_center = start + forward * shaft_len;

    auto ring_point = [&](const glm::vec3& center, float radius, uint32_t i) -> glm::vec3
    {
        const float theta = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i)) / static_cast<float>(N);
        return center + (std::cos(theta) * right + std::sin(theta) * up) * radius;
    };

    // 0 .. N-1: shaft base
    for (uint32_t i = 0; i < N; ++i)
        verts_.push_back({ ring_point(start, shaft_radius, i), color });

    // N .. 2N-1: shaft top
    for (uint32_t i = 0; i < N; ++i)
        verts_.push_back({ ring_point(shaft_top_center, shaft_radius, i), color });

    // 2N .. 3N-1: tip base (same plane as shaft top, wider radius)
    for (uint32_t i = 0; i < N; ++i)
        verts_.push_back({ ring_point(shaft_top_center, tip_radius, i), color });

    const uint32_t i_apex = base + 3 * N;
    verts_.push_back({ end, color });

    const uint32_t i_butt = base + 3 * N + 1;
    verts_.push_back({ start, color });

    auto idx = [base](uint32_t local) -> uint32_t
    {
        return base + local;
    };

    // Shaft sides: base ring [0..N) to top ring [N..2N)
    // CCW from outside (front face COUNTER_CLOCKWISE in pipeline).
    for (uint32_t i = 0; i < N; ++i)
    {
        const uint32_t j = (i + 1) % N;
        const uint32_t b0 = idx(i);
        const uint32_t b1 = idx(j);
        const uint32_t t0 = idx(N + i);
        const uint32_t t1 = idx(N + j);
        append_tri(b0, b1, t1);
        append_tri(b0, t1, t0);
    }

    // Cone tip: tip base [2N..3N) to apex
    for (uint32_t i = 0; i < N; ++i)
    {
        const uint32_t j = (i + 1) % N;
        const uint32_t tb0 = idx(2 * N + i);
        const uint32_t tb1 = idx(2 * N + j);
        append_tri(tb0, tb1, i_apex);
    }

    // Butt cap at start: fan, outward is -forward
    for (uint32_t i = 0; i < N; ++i)
    {
        const uint32_t j = (i + 1) % N;
        append_tri(i_butt, idx(j), idx(i));
    }
}
