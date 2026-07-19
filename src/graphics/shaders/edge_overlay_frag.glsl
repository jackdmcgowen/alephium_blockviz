#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D edgeSamp;
layout(binding = 1) uniform sampler2D colorSamp;

layout(push_constant) uniform PC
{
    float intensity; // global boost (white edge * instance color * intensity)
    float pad0;
    float pad1;
    float pad2;
} pc;

void main()
{
    float e = texture(edgeSamp, vUV).r;
    vec4 c = texture(colorSamp, vUV);
    // White Sobel edge * instance color * (global * per-instance intensity).
    float boost = max(pc.intensity, 0.0) * max(c.a, 0.0);
    float a = clamp(e * boost, 0.0, 1.0);
    outColor = vec4(c.rgb * a, a);
}
