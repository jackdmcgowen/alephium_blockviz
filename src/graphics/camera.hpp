#pragma once

// Pure 3D camera + view-frustum (no Vulkan). Matrices match engine GLM settings
// (left-handed, depth zero-to-one) when built via camera.cpp.
#include "graphics/gpu_pub_lib.h"

#include <glm/glm.hpp>

struct Frustum
{
    // Plane: n.x*x + n.y*y + n.z*z + d  (n = xyz, d = w). Inside half-space: >= 0.
    glm::vec4 planes[6]{};

    bool intersects_aabb(const glm::vec3& center, const glm::vec3& half_extents) const;
    bool intersects_sphere(const glm::vec3& center, float radius) const;
};

struct Camera
{
    static constexpr float kDefaultFovYRad = 0.785398163f; // 45 deg
    static constexpr float kDefaultNearZ   = 1.0f;
    static constexpr float kDefaultFarZ    = 5000.0f;

    // Pose
    glm::vec3 eye{ 0.f, 0.f, 0.f };
    glm::vec3 forward{ 0.f, 0.f, 1.f }; // unit
    glm::vec3 up{ 0.f, -1.f, 0.f };

    // Projection
    float fov_y_rad = kDefaultFovYRad;
    float aspect    = 1.f;
    float near_z    = kDefaultNearZ;
    float far_z     = kDefaultFarZ;

    glm::mat4 view_matrix() const;
    glm::mat4 proj_matrix() const;
    glm::mat4 view_proj() const;
    Frustum   frustum() const;

    CameraUBO to_ubo(float meters_per_second) const;
};
