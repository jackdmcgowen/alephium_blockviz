#version 450
#extension GL_EXT_mesh_shader : require
// Amplification (task) stage: frustum-cull a batch of cube instances and emit
// one mesh workgroup per survivor. Planes match camera.cpp / instance_cull.comp.

layout(local_size_x = 32) in;

struct Instance
{
    vec3  pos;
    float scale;
    vec3  color;
    float alpha;
};

layout(binding = 0) uniform UniformBufferObject
{
    mat4  view;
    mat4  proj;
    vec3  lightPos;
    float pad1;
    vec3  viewPos;
    float pad2;
    float anim_scale;
    float anim_alpha;
    float anim_time;
    float pad3;
} ubo;

layout(std430, binding = 1) readonly buffer Instances
{
    Instance instances[];
};

// Shared with mesh stage (must match cube.mesh.glsl).
struct TaskPayload
{
    uint count;
    uint indices[32];
};
taskPayloadSharedEXT TaskPayload payload;

layout(push_constant) uniform PC
{
    vec4  planes[6];
    uint  instance_count;
    float half_extent;
    float pad0;
    float pad1;
} pc;

shared uint s_visible;
shared uint s_indices[32];

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
    if (gl_LocalInvocationIndex == 0u)
        s_visible = 0u;
    barrier();

    const uint base = gl_WorkGroupID.x * 32u;
    const uint i = base + gl_LocalInvocationIndex;
    if (i < pc.instance_count)
    {
        Instance inst = instances[i];
        float s = max(inst.scale * ubo.anim_scale, 1e-4);
        if (aabb_visible(inst.pos, s))
        {
            uint slot = atomicAdd(s_visible, 1u);
            if (slot < 32u)
                s_indices[slot] = i;
        }
    }
    barrier();

    if (gl_LocalInvocationIndex == 0u)
    {
        payload.count = s_visible;
        for (uint k = 0u; k < s_visible && k < 32u; ++k)
            payload.indices[k] = s_indices[k];
        EmitMeshTasksEXT(s_visible, 1, 1);
    }
}
