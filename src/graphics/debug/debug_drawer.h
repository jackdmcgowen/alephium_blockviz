#pragma once

// CPU-only debug geometry builder. No Vulkan.
// Triangle meshes (arrows) + line lists (wire boxes / links) — lines are edge pairs only (no face diagonals).
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct DebugVertex
{
    glm::vec3 position;
    glm::vec4 color;
};

class DebugDrawer
{
public:
    static constexpr uint32_t kDefaultRadialSegments = 16;
    static constexpr float    kMaxTipFraction        = 0.9f;
    static constexpr float    kDefaultShaftToTip     = 0.35f;

    void clear();

    // World-space 3D arrow: cylindrical shaft + cone tip (absolute tip size).
    // grow_u in [0,1]: 0 = invisible, 1 = full length. Tip marches start→end; alpha eases with grow.
    void add_arrow(const glm::vec3& start,
                   const glm::vec3& end,
                   const glm::vec4& color,
                   float tip_length,
                   float tip_radius,
                   float shaft_radius = -1.0f,
                   uint32_t radial_segments = kDefaultRadialSegments,
                   float grow_u = 1.0f);

    // Append a mesh; indices are local (0 .. vert_count-1) and are rebased.
    void add_mesh(const DebugVertex* verts, uint32_t vert_count,
                  const uint32_t* indices, uint32_t index_count);

    // LINE_LIST: one segment (two vertices). No face diagonals.
    void add_line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

    // 12 cube edges only (true box outline — not triangle wireframe).
    void add_wire_box(const glm::vec3& center, float half_extent, const glm::vec4& color);

    // Large double-sided quad in the XY plane at constant world Z (barrier / slab marker).
    // half_extent is half-width in both X and Y (total span 2*half_extent).
    void add_z_plane_quad(float z, float half_extent, const glm::vec4& color);

    const DebugVertex* vertices() const { return verts_.empty() ? nullptr : verts_.data(); }
    uint32_t vertex_count() const { return static_cast<uint32_t>(verts_.size()); }
    const uint32_t* indices() const { return indices_.empty() ? nullptr : indices_.data(); }
    uint32_t index_count() const { return static_cast<uint32_t>(indices_.size()); }

    const DebugVertex* line_vertices() const { return line_verts_.empty() ? nullptr : line_verts_.data(); }
    uint32_t line_vertex_count() const { return static_cast<uint32_t>(line_verts_.size()); }

private:
    void append_tri(uint32_t i0, uint32_t i1, uint32_t i2);
    static void build_basis(const glm::vec3& forward, glm::vec3& right, glm::vec3& up);

    std::vector<DebugVertex> verts_;
    std::vector<uint32_t>    indices_;
    std::vector<DebugVertex> line_verts_; // pairs for LINE_LIST
};
