#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D edgeSamp;

layout(push_constant) uniform PC
{
    vec4 highlight; // rgb + intensity
} pc;

void main()
{
    float e = texture(edgeSamp, vUV).r;
    // Additive-style edge tint (premultiplied alpha for blend)
    float a = clamp(e * pc.highlight.a, 0.0, 1.0);
    outColor = vec4(pc.highlight.rgb * a, a);
}
