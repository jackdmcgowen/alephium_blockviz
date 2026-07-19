#version 450
// Outline pass: write flat instance color for Sobel tint (edge * color).
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main()
{
    // RGB = outline tint from app; A = per-instance intensity (e.g. 1.35).
    outColor = vec4(fragColor, max(fragAlpha, 0.0));
}
