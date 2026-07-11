#version 450

// Depth Sobel edge filter — runs on CMP queue (async compute).
// Samples depth (sampled as float texture), writes edge strength to R8 storage.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0) uniform sampler2D depthSamp;
layout(binding = 1, r8) writeonly uniform image2D edgeOut;

layout(push_constant) uniform PC
{
    float strength;
    float threshold;
    float inv_width;
    float inv_height;
} pc;

float sample_depth(vec2 uv)
{
    return texture(depthSamp, uv).r;
}

void main()
{
    const ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(edgeOut);
    if (p.x >= size.x || p.y >= size.y)
        return;

    const vec2 uv = (vec2(p) + vec2(0.5)) * vec2(pc.inv_width, pc.inv_height);
    const float dx = pc.inv_width;
    const float dy = pc.inv_height;

    // 3x3 Sobel
    const float tl = sample_depth(uv + vec2(-dx, -dy));
    const float tc = sample_depth(uv + vec2( 0.0, -dy));
    const float tr = sample_depth(uv + vec2( dx, -dy));
    const float ml = sample_depth(uv + vec2(-dx,  0.0));
    const float mr = sample_depth(uv + vec2( dx,  0.0));
    const float bl = sample_depth(uv + vec2(-dx,  dy));
    const float bc = sample_depth(uv + vec2( 0.0,  dy));
    const float br = sample_depth(uv + vec2( dx,  dy));

    const float gx = -tl + tr - 2.0 * ml + 2.0 * mr - bl + br;
    const float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
    float edge = sqrt(gx * gx + gy * gy);
    edge = max(0.0, edge - pc.threshold) * pc.strength;
    edge = clamp(edge, 0.0, 1.0);

    imageStore(edgeOut, p, vec4(edge, 0.0, 0.0, 1.0));
}
