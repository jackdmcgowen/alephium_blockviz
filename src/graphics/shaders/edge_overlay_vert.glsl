#version 450

// Fullscreen triangle (no VBO) for edge composite pass.

layout(location = 0) out vec2 vUV;

void main()
{
    // gl_VertexIndex 0,1,2 → covers clip space
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
