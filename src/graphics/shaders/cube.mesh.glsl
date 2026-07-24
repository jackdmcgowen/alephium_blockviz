#version 450
#extension GL_EXT_mesh_shader : require
// Meshlet for one cube instance (from task payload index).
// Emits classic 8 verts / 12 tris (same winding as CUBE_INDICES / cube_mesh_only).
// Hardware back-face cull handles hidden faces. Fragment interface matches frag.glsl.

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 8, max_primitives = 12) out;

struct Instance
{
    vec3  pos;
    float scale;
    vec3  color;
    float alpha;
};

struct TaskPayload
{
    uint count;
    uint indices[32];
};
taskPayloadSharedEXT TaskPayload payload;

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

layout(location = 0) out vec3 outColor[];
layout(location = 1) out vec3 outPos[];
layout(location = 2) out vec3 outNormal[];
layout(location = 3) out float outAlpha[];

// Match GraphicsSystem::CUBE_VERTICES / cube_mesh_only.mesh.glsl
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

// Match CUBE_INDICES (CCW, FRONT_FACE_COUNTER_CLOCKWISE)
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
    const uint slot = gl_WorkGroupID.x;
    if (slot >= payload.count)
    {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    const uint inst_i = payload.indices[slot];
    Instance inst = instances[inst_i];
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
