#version 450
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 lightPos;    // Added light position
    float pad1;
    vec3 viewPos;     // Added camera position
    float pad2;
    float meters;
} ubo;


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in vec3 inInstanceColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragPos;    // Added for Phong
layout(location = 2) out vec3 fragNormal; // Added for Phong

void main() {
    mat3 scalMat = mat3(
        vec3( 1.0, 0.0, 0.0 ),
        vec3( 0.0, 1.0, 0.0 ),
        vec3( 0.0, 0.0, ubo.meters ) );
    
    vec3 scalPos = scalMat * inInstancePos;

    mat4 posMat = mat4(
        vec4( 1.0, 0.0, 0.0, 0.0),
        vec4( 0.0, 1.0, 0.0, 0.0),
        vec4( 0.0, 0.0, 1.0, 0.0),
        vec4( scalPos, 1.0) );
    
    vec4 worldPos = posMat * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    fragPos = worldPos.xyz;
    fragNormal = normalize(inNormal);
    fragColor = inInstanceColor;
}