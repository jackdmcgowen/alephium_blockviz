#include "graphics/camera.hpp"

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace
{
void normalize_plane(glm::vec4& p)
{
    const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 1e-8f)
        p /= len;
}

// Extract frustum planes from clip matrix M = proj * view (column-major GLM).
// Positive half-space is inside after normalization.
void extract_planes(const glm::mat4& m, glm::vec4 out[6])
{
    // Left / Right / Bottom / Top / Near / Far
    out[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
    out[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
    out[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
    out[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
    // Depth 0–1: near uses row z only; far is row w − row z.
    out[4] = glm::vec4(m[0][2], m[1][2], m[2][2], m[3][2]);
    out[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);

    for (int i = 0; i < 6; ++i)
        normalize_plane(out[i]);
}
} // namespace

bool Frustum::intersects_aabb(const glm::vec3& center, const glm::vec3& half_extents) const
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec4& pl = planes[i];
        // Positive vertex along plane normal (most-inside corner for outside test).
        const glm::vec3 p{
            center.x + (pl.x >= 0.f ? half_extents.x : -half_extents.x),
            center.y + (pl.y >= 0.f ? half_extents.y : -half_extents.y),
            center.z + (pl.z >= 0.f ? half_extents.z : -half_extents.z),
        };
        if (pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w < 0.f)
            return false;
    }
    return true;
}

bool Frustum::intersects_sphere(const glm::vec3& center, float radius) const
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec4& pl = planes[i];
        if (pl.x * center.x + pl.y * center.y + pl.z * center.z + pl.w < -radius)
            return false;
    }
    return true;
}

glm::mat4 Camera::view_matrix() const
{
    glm::vec3 f = forward;
    const float flen = glm::length(f);
    if (flen < 1e-6f)
        f = glm::vec3(0.f, 0.f, 1.f);
    else
        f /= flen;
    return glm::lookAt(eye, eye + f, up);
}

glm::mat4 Camera::proj_matrix() const
{
    const float a = (aspect > 1e-6f) ? aspect : 1.f;
    return glm::perspective(fov_y_rad, a, near_z, far_z);
}

glm::mat4 Camera::view_proj() const
{
    return proj_matrix() * view_matrix();
}

Frustum Camera::frustum() const
{
    Frustum f{};
    extract_planes(view_proj(), f.planes);
    return f;
}

CameraUBO Camera::to_ubo() const
{
    CameraUBO cam{};
    glm::vec3 f = forward;
    const float flen = glm::length(f);
    if (flen < 1e-6f)
        f = glm::vec3(0.f, 0.f, 1.f);
    else
        f /= flen;

    cam.view = view_matrix();
    cam.proj = proj_matrix();
    cam.view_pos = eye;
    // Key light fixed relative to the Z-track (not look dir) so free-look does not
    // swing the light. Offset: +X side, -Y "up" (camera up is (0,-1,0)), slightly +Z.
    cam.light_pos = eye + glm::vec3(55.f, -90.f, 40.f);
    return cam;
}
