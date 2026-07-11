#pragma once

// Shared vertex/instance layouts for cube + picker pipelines (E4).
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <cstdint>

struct VertexNormal
{
    glm::vec3 pos;
    glm::vec3 normal;
};

struct InstanceData
{
    glm::vec3 pos;
    glm::vec3 color;
};

struct UniformBufferObject
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 lightPos;
    glm::float32 pad1;
    glm::vec3 viewPos;
    glm::float32 pad2;
};

struct PickerPushConstants
{
    uint32_t mouseX;
    uint32_t mouseY;
    uint32_t instanceOffset;
};
