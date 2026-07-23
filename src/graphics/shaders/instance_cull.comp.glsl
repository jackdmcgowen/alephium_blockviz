#version 450
// Frustum-cull cube instances and compact into a visible list for indirect draw.
// Plane convention matches camera.cpp: inside half-space n·x + d >= 0.

layout(local_size_x = 64) in;

struct Instance
{
    vec3  pos;
    float scale;
    vec3  color;
    float alpha;
};

// Must match VkDrawIndexedIndirectCommand packing (20 bytes, no padding).
struct DrawIndexedIndirect
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout(std430, binding = 0) readonly buffer InInstances
{
    Instance in_instances[];
};

layout(std430, binding = 1) writeonly buffer OutInstances
{
    Instance out_instances[];
};

layout(std430, binding = 2) buffer DrawCmd
{
    DrawIndexedIndirect draw;
};

layout(push_constant) uniform PC
{
    vec4  planes[6];
    uint  count;
    float half_extent; // world half-extent multiplier (mesh ±1 * scale)
    float pad0;
    float pad1;
} pc;

bool aabb_visible(vec3 center, float scale)
{
    vec3 h = vec3(max(scale, 1e-4) * pc.half_extent);
    for (int i = 0; i < 6; ++i)
    {
        vec4 pl = pc.planes[i];
        vec3 p = center + vec3(
            pl.x >= 0.0 ? h.x : -h.x,
            pl.y >= 0.0 ? h.y : -h.y,
            pl.z >= 0.0 ? h.z : -h.z);
        if (dot(pl.xyz, p) + pl.w < 0.0)
            return false;
    }
    return true;
}

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count)
        return;

    Instance inst = in_instances[i];
    if (!aabb_visible(inst.pos, inst.scale))
        return;

    uint slot = atomicAdd(draw.instanceCount, 1u);
    out_instances[slot] = inst;
}
