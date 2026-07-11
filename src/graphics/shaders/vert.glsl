#version 450
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightPos;
    float pad1;
    vec3 viewPos;
    float pad2;
} ubo;


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in vec3 inInstanceColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec3 fragNormal;

void main() {
    vec4 worldPos = vec4(inPosition + inInstancePos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragPos = worldPos.xyz;
    fragNormal = normalize(inNormal);
    fragColor = inInstanceColor;
}
