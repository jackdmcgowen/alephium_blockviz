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

 //flat - prevents interpolation
layout(location = 4) flat out uint vInstanceID;

void main() {

    mat4 posMat = mat4(
        vec4( 1.0, 0.0, 0.0, 0.0),
        vec4( 0.0, 1.0, 0.0, 0.0),
        vec4( 0.0, 0.0, 1.0, 0.0),
        vec4( 0.0, 0.0, ubo.meters, 1.0) );
    
    vec4 worldPos = posMat * vec4(inPosition + inInstancePos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
     //pass the instance index through (flat interpolated)
    vInstanceID = gl_InstanceIndex;
    
}