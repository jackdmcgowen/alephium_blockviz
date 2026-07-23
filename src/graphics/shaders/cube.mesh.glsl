#version 450
#extension GL_EXT_mesh_shader : require
// Per-instance cube meshlet from instance SSBO (post-cull compact or full list).
// Emits 8 corner verts + 12 tris matching classic CUBE_VERTICES / CUBE_INDICES.
// Fragment interface matches frag.glsl (locations 0–3).

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 8, max_primitives = 12) out;

struct Instance
{
    vec3  pos;
    float scale;
    vec3  color;
    float alpha;
};

// Same packing as VkDrawIndexedIndirectCommand (PR2 cull args).
struct DrawIndexedIndirect
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
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

layout(std430, binding = 2) readonly buffer DrawCmd
{
    DrawIndexedIndirect draw;
};

layout(push_constant) uniform PC
{
    uint host_count;   // host upload count (dispatch upper bound)
    uint use_gpu_cull; // 1: clamp by draw.instanceCount (compact buffer)
} pc;

layout(location = 0) out vec3 outColor[];
layout(location = 1) out vec3 outPos[];
layout(location = 2) out vec3 outNormal[];
layout(location = 3) out float outAlpha[];

// Classic cube corners (±1); normals = normalize(pos) as in GraphicsSystem::CUBE_VERTICES.
const vec3 kCorner[8] = vec3[](
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0,  1.0)
);

// Same winding as CUBE_INDICES (uint16 list of 36 indices → 12 triangles).
const uvec3 kTri[12] = uvec3[](
    uvec3(6, 0, 2), uvec3(6, 1, 0),
    uvec3(4, 0, 1), uvec3(0, 4, 3),
    uvec3(0, 3, 2), uvec3(3, 5, 2),
    uvec3(5, 6, 2), uvec3(6, 5, 7),
    uvec3(6, 7, 1), uvec3(1, 7, 4),
    uvec3(3, 4, 7), uvec3(7, 5, 3)
);

void main()
{
    const uint i = gl_WorkGroupID.x;
    uint limit = pc.host_count;
    if (pc.use_gpu_cull != 0u)
        limit = min(limit, draw.instanceCount);

    if (i >= limit)
    {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    Instance inst = instances[i];
    const float s = max(inst.scale * ubo.anim_scale, 1e-4);
    const float a = clamp(inst.alpha * ubo.anim_alpha, 0.0, 1.0);

    SetMeshOutputsEXT(8, 12);

    for (uint v = 0u; v < 8u; ++v)
    {
        const vec3 local = kCorner[v];
        const vec3 world = local * s + inst.pos;
        gl_MeshVerticesEXT[v].gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
        outPos[v] = world;
        outNormal[v] = normalize(local);
        outColor[v] = inst.color;
        outAlpha[v] = a;
    }

    for (uint t = 0u; t < 12u; ++t)
        gl_PrimitiveTriangleIndicesEXT[t] = kTri[t];
}
