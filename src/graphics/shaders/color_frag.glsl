#version 460

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
    
    // Optional: simple distance-based fade for very long arrows
    // outColor.a *= clamp(1.0 - (gl_FragCoord.z * 0.5), 0.0, 1.0);
}
