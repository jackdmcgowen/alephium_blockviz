#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightPos;
    float pad1;
    vec3 viewPos;
    float pad2;
    float anim_scale;
    float anim_alpha;
    float anim_time;
    float pad3;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in float inInstanceScale;
layout(location = 4) in vec3 inInstanceColor;
layout(location = 5) in float inInstanceAlpha;

layout(location = 4) flat out uint vInstanceID;

void main() {
    float s = max(inInstanceScale * ubo.anim_scale, 1e-4);
    vec3 world = inPosition * s + inInstancePos;
    gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
    vInstanceID = gl_InstanceIndex;
}
