#version 450
#extension GL_EXT_mesh_shader : require
// Meshlet for one cube instance (from task payload index).
// Cluster cone / face cull: emit only faces with outward normal toward the camera.
// Fragment interface matches frag.glsl (locations 0–3).

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 24, max_primitives = 12) out;

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

// 6 faces × 4 corner indices into kCorner (CCW when viewed from outside, match classic).
const vec3 kCorner[8] = vec3[](
    vec3(-1.0, -1.0,  1.0), // 0
    vec3( 1.0, -1.0,  1.0), // 1
    vec3(-1.0, -1.0, -1.0), // 2
    vec3(-1.0,  1.0, -1.0), // 3
    vec3(-1.0,  1.0,  1.0), // 4
    vec3( 1.0,  1.0, -1.0), // 5
    vec3( 1.0, -1.0, -1.0), // 6
    vec3( 1.0,  1.0,  1.0)  // 7
);

// Face: normal (axis-aligned), 4 verts, two tris.
// Normals in local cube space (uniform scale preserves).
const vec3 kFaceN[6] = vec3[](
    vec3( 0.0, -1.0,  0.0), // -Y  (0,1,6,2 area)
    vec3( 0.0,  1.0,  0.0), // +Y
    vec3(-1.0,  0.0,  0.0), // -X
    vec3( 1.0,  0.0,  0.0), // +X
    vec3( 0.0,  0.0,  1.0), // +Z
    vec3( 0.0,  0.0, -1.0)  // -Z
);

const uvec4 kFaceV[6] = uvec4[](
    uvec4(0, 1, 6, 2), // -Y bottom
    uvec4(4, 7, 5, 3), // +Y top
    uvec4(0, 2, 3, 4), // -X
    uvec4(1, 7, 5, 6), // +X
    uvec4(0, 4, 7, 1), // +Z
    uvec4(2, 6, 5, 3)  // -Z
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
    const vec3 center = inst.pos;
    const vec3 to_eye = ubo.viewPos - center;

    // Cluster cone / face cull: keep faces whose outward normal faces the camera.
    uint face_mask = 0u;
    uint nfaces = 0u;
    for (uint f = 0u; f < 6u; ++f)
    {
        // With uniform scale, local face normal direction is unchanged in world.
        if (dot(kFaceN[f], to_eye) > 0.0)
        {
            face_mask |= (1u << f);
            nfaces++;
        }
    }

    // Degenerate (camera inside cube or numerical): emit full cube.
    if (nfaces == 0u)
    {
        face_mask = 0x3Fu;
        nfaces = 6u;
    }

    const uint nverts = nfaces * 4u;
    const uint nprims = nfaces * 2u;
    SetMeshOutputsEXT(nverts, nprims);

    uint vbase = 0u;
    uint tbase = 0u;
    for (uint f = 0u; f < 6u; ++f)
    {
        if ((face_mask & (1u << f)) == 0u)
            continue;

        const vec3 n = kFaceN[f];
        const uvec4 fv = kFaceV[f];
        for (uint k = 0u; k < 4u; ++k)
        {
            const vec3 local = kCorner[fv[k]];
            const vec3 world = local * s + center;
            const uint vi = vbase + k;
            gl_MeshVerticesEXT[vi].gl_Position = ubo.proj * ubo.view * vec4(world, 1.0);
            outPos[vi] = world;
            outNormal[vi] = n;
            outColor[vi] = inst.color;
            outAlpha[vi] = a;
        }
        // Two tris (CCW from outside).
        gl_PrimitiveTriangleIndicesEXT[tbase + 0u] =
            uvec3(vbase + 0u, vbase + 1u, vbase + 2u);
        gl_PrimitiveTriangleIndicesEXT[tbase + 1u] =
            uvec3(vbase + 0u, vbase + 2u, vbase + 3u);
        vbase += 4u;
        tbase += 2u;
    }
}
