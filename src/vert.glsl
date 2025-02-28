#version 450
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inInstancePos;
layout(location = 2) in vec3 inInstanceColor;

layout(location = 0) out vec3 fragColor;

void main() {

    mat4 posMat = mat4(
        vec4( 1.0, 0.0, 0.0, 0.0),
        vec4( 0.0, 1.0, 0.0, 0.0),
        vec4( 0.0, 0.0, 1.0, 0.0),
        vec4( inInstancePos, 1.0) );
        
    gl_Position = ubo.proj * ubo.view * vec4(inPosition + inInstancePos, 1.0);
    fragColor = inInstanceColor;
}