#include "graphics/pch.h"
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

void DebugDrawer::add_z_plane_quad(float z, float half_extent, const glm::vec4& color)
{
    const float s = half_extent > 1e-4f ? half_extent : 1.f;
    const DebugVertex v[4] = {
        { glm::vec3(-s, -s, z), color },
        { glm::vec3( s, -s, z), color },
        { glm::vec3( s,  s, z), color },
        { glm::vec3(-s,  s, z), color },
    };
    // Both windings — tri pipeline culls back faces.
    static const uint32_t front[6] = { 0, 1, 2, 0, 2, 3 };
    static const uint32_t back[6]  = { 0, 2, 1, 0, 3, 2 };
    add_mesh(v, 4, front, 6);
    add_mesh(v, 4, back, 6);
    // Thin edge ring for readability at low alpha.
    const glm::vec4 edge(color.r, color.g, color.b, std::min(1.f, color.a * 2.5f));
    add_line(v[0].position, v[1].position, edge);
    add_line(v[1].position, v[2].position, edge);
    add_line(v[2].position, v[3].position, edge);
    add_line(v[3].position, v[0].position, edge);
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
                            uint32_t radial_segments,
                            float grow_u)
{
    add_arrow(start, end, color, color, tip_length, tip_radius, shaft_radius, radial_segments,
              grow_u);
}

void DebugDrawer::add_arrow(const glm::vec3& start,
                            const glm::vec3& end,
                            const glm::vec4& shaft_rgba,
                            const glm::vec4& tip_rgba,
                            float tip_length,
                            float tip_radius,
                            float shaft_radius,
                            uint32_t radial_segments,
                            float grow_u)
{
    // Smoothstep grow + early out
    float u = std::clamp(grow_u, 0.0f, 1.0f);
    if (u <= 1e-4f)
        return;
    u = u * u * (3.0f - 2.0f * u);

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

    // Grow: tip marches start→end; only a prefix of the segment is drawn.
    const float drawn_len = total * u;
    if (drawn_len < 1e-4f)
        return;

    const glm::vec3 forward = dir / total;
    const glm::vec3 tip_pos = start + forward * drawn_len;

    float tip_len = std::min(tip_length, drawn_len * kMaxTipFraction);
    if (tip_len < 1e-5f && drawn_len > 1e-4f)
        tip_len = std::min(drawn_len * 0.25f, tip_length > 0.0f ? tip_length : drawn_len * 0.25f);

    const float shaft_len = std::max(0.0f, drawn_len - tip_len);

    // Fade in with grow (keeps early frames soft). Dual RGBA: shaft vs tip cone.
    const float a_mul = (0.2f + 0.8f * u);
    glm::vec4 col_shaft = shaft_rgba;
    glm::vec4 col_tip = tip_rgba;
    col_shaft.a *= a_mul;
    col_tip.a *= a_mul;

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
        verts_.push_back({ ring_point(start, shaft_radius, i), col_shaft });

    // N .. 2N-1: shaft top
    for (uint32_t i = 0; i < N; ++i)
        verts_.push_back({ ring_point(shaft_top_center, shaft_radius, i), col_shaft });

    // 2N .. 3N-1: tip base (same plane as shaft top, wider radius)
    for (uint32_t i = 0; i < N; ++i)
        verts_.push_back({ ring_point(shaft_top_center, tip_radius, i), col_tip });

    const uint32_t i_apex = base + 3 * N;
    verts_.push_back({ tip_pos, col_tip });

    const uint32_t i_butt = base + 3 * N + 1;
    verts_.push_back({ start, col_shaft });

    auto idx = [base](uint32_t local) -> uint32_t
    {
        return base + local;
    };

    // Shaft sides: base ring [0..N) to top ring [N..2N)
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

    // Butt cap at start
    for (uint32_t i = 0; i < N; ++i)
    {
        const uint32_t j = (i + 1) % N;
        append_tri(i_butt, idx(j), idx(i));
    }
}
