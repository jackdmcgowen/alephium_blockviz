#pragma once

// CPU-only debug geometry builder. No Vulkan.
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
    // radial_segments defaults to 16 for a readable cone silhouette.
    void add_arrow(const glm::vec3& start,
                   const glm::vec3& end,
                   const glm::vec4& color,
                   float tip_length,
                   float tip_radius,
                   float shaft_radius = -1.0f,
                   uint32_t radial_segments = kDefaultRadialSegments);

    // Append a mesh; indices are local (0 .. vert_count-1) and are rebased.
    void add_mesh(const DebugVertex* verts, uint32_t vert_count,
                  const uint32_t* indices, uint32_t index_count);

    const DebugVertex* vertices() const { return verts_.empty() ? nullptr : verts_.data(); }
    uint32_t vertex_count() const { return static_cast<uint32_t>(verts_.size()); }
    const uint32_t* indices() const { return indices_.empty() ? nullptr : indices_.data(); }
    uint32_t index_count() const { return static_cast<uint32_t>(indices_.size()); }

private:
    void append_tri(uint32_t i0, uint32_t i1, uint32_t i2);
    static void build_basis(const glm::vec3& forward, glm::vec3& right, glm::vec3& up);

    std::vector<DebugVertex> verts_;
    std::vector<uint32_t>    indices_;
};
