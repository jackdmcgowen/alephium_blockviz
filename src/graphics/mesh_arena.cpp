#include "graphics/pch.h"
#include "mesh_arena.h"
#include "debug/debug_drawer.h"
#include "gpu_prv_lib.h"

#include <cstring>
#include <cstdio>
#include <vector>

MeshArena::~MeshArena()
{
    destroy();
}

bool MeshArena::create(VkDevice device,
                       VkPhysicalDeviceMemoryProperties* mem_props,
                       BufferManager* buffer_manager,
                       VkFormat color_format,
                       VkFormat depth_format,
                       VkSampleCountFlagBits samples,
                       uint32_t max_vertices,
                       uint32_t max_indices,
                       uint32_t max_line_verts)
{
    destroy();

    if (!buffer_manager)
        return false;

    device_       = device;
    mem_props_    = mem_props;
    buffers_      = buffer_manager;
    color_format_ = color_format;
    depth_format_ = depth_format;
    samples_      = samples;
    max_vertices_ = max_vertices;
    max_indices_  = max_indices;
    max_line_verts_ = max_line_verts;

    const VkMemoryPropertyFlags host =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    vertex_ = buffers_->create(BufferDesc{
        max_vertices_ * sizeof(DebugVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, "debug.vb"});
    index_ = buffers_->create(BufferDesc{
        max_indices_ * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, host, "debug.ib"});
    line_ = buffers_->create(BufferDesc{
        max_line_verts_ * sizeof(DebugVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, "debug.lines"});

    vertex_mapped_ = vertex_.map(device_);
    index_mapped_ = index_.map(device_);
    line_mapped_ = line_.map(device_);
    if (!vertex_mapped_ || !index_mapped_ || !line_mapped_)
        return false;

    return create_pipelines();
}

void MeshArena::destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    if (tri_pipeline_ != VK_NULL_HANDLE)
    {
        destroy_pipeline(device_, tri_pipeline_);
        tri_pipeline_ = VK_NULL_HANDLE;
    }
    if (line_pipeline_ != VK_NULL_HANDLE)
    {
        destroy_pipeline(device_, line_pipeline_);
        line_pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE)
    {
        destroy_pipeline_layout(device_, pipeline_layout_);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (buffers_)
    {
        if (vertex_mapped_) { vertex_.unmap(device_); vertex_mapped_ = nullptr; }
        if (index_mapped_) { index_.unmap(device_); index_mapped_ = nullptr; }
        if (line_mapped_) { line_.unmap(device_); line_mapped_ = nullptr; }
        buffers_->destroy(vertex_);
        buffers_->destroy(index_);
        buffers_->destroy(line_);
        buffers_ = nullptr;
    }

    uploaded_index_count_ = uploaded_vertex_count_ = uploaded_line_count_ = 0;
    device_ = VK_NULL_HANDLE;
}

void MeshArena::upload(const DebugDrawer& drawer)
{
    uploaded_index_count_ = uploaded_vertex_count_ = uploaded_line_count_ = 0;

    const uint32_t vc = drawer.vertex_count();
    const uint32_t ic = drawer.index_count();
    const uint32_t lc = drawer.line_vertex_count();

    if (vc > 0 && ic > 0 && vertex_mapped_ && index_mapped_)
    {
        if (vc <= max_vertices_ && ic <= max_indices_)
        {
            std::memcpy(vertex_mapped_, drawer.vertices(), vc * sizeof(DebugVertex));
            std::memcpy(index_mapped_, drawer.indices(), ic * sizeof(uint32_t));
            uploaded_vertex_count_ = vc;
            uploaded_index_count_ = ic;
        }
        else
        {
            std::fprintf(stderr, "[MeshArena] triangle overflow v=%u i=%u\n", vc, ic);
        }
    }

    if (lc > 0 && line_mapped_)
    {
        if (lc <= max_line_verts_ && (lc % 2u) == 0u)
        {
            std::memcpy(line_mapped_, drawer.line_vertices(), lc * sizeof(DebugVertex));
            uploaded_line_count_ = lc;
        }
        else
        {
            std::fprintf(stderr, "[MeshArena] line overflow lc=%u\n", lc);
        }
    }
}

bool MeshArena::create_pipelines()
{
    // Shared pipeline module (PipelineType::_3D): layout + graphics PSOs.
    // Depth test ON, depth write OFF so translucent debug meshes do not occlude
    // each other incorrectly against cubes already written to the depth buffer.
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(glm::mat4);

    try
    {
        pipeline_layout_ = create_pipeline_layout(device_, nullptr, 0, &push, 1);
    }
    catch (...)
    {
        return false;
    }

    std::vector<uint8_t> vert_code, frag_code;
    load_shader_source("debug_mesh_vert.spv", vert_code);
    load_shader_source("color_frag.spv", frag_code);

    VkShaderModule vert_module = VK_NULL_HANDLE, frag_module = VK_NULL_HANDLE;
    create_shader_module(device_, vert_module, vert_code);
    create_shader_module(device_, frag_module, frag_code);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(DebugVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[2] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugVertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DebugVertex, color) },
    };

    auto make_graphics = [&](VkPrimitiveTopology topo, VkCullModeFlags cull,
                             VkPipeline* out) -> bool {
        GraphicsPipelineCreateInfo ginfo{};
        ginfo.layout = pipeline_layout_;
        ginfo.stages = stages;
        ginfo.stage_count = 2;
        ginfo.bindings = &binding;
        ginfo.binding_count = 1;
        ginfo.attributes = attribs;
        ginfo.attribute_count = 2;
        ginfo.topology = topo;
        ginfo.cull_mode = cull;
        ginfo.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        ginfo.depth_test = true;
        ginfo.depth_write = false;
        ginfo.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
        ginfo.blend_mode = PipelineBlendMode::Alpha;
        ginfo.samples = samples_;
        ginfo.color_format = color_format_;
        ginfo.depth_format = depth_format_;
        ginfo.color_attachment_count = 1;
        ginfo.dynamic_viewport_scissor = true;
        try
        {
            *out = create_graphics_pipeline(device_, ginfo);
            return *out != VK_NULL_HANDLE;
        }
        catch (...)
        {
            *out = VK_NULL_HANDLE;
            return false;
        }
    };

    const bool ok_tri =
        make_graphics(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT, &tri_pipeline_);
    const bool ok_line =
        make_graphics(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_CULL_MODE_NONE, &line_pipeline_);

    destroy_shader_module(device_, vert_module);
    destroy_shader_module(device_, frag_module);

    return ok_tri && ok_line;
}

void MeshArena::draw(VkCommandBuffer cmd, const glm::mat4& view_proj)
{
    if (pipeline_layout_ == VK_NULL_HANDLE)
        return;

    if (uploaded_index_count_ > 0 && tri_pipeline_ != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tri_pipeline_);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &view_proj);
        VkDeviceSize offset = 0;
        VkBuffer vb = vertex_.handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, index_.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, uploaded_index_count_, 1, 0, 0, 0);
    }

    if (uploaded_line_count_ > 0 && line_pipeline_ != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &view_proj);
        VkDeviceSize offset = 0;
        VkBuffer lb = line_.handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &lb, &offset);
        vkCmdDraw(cmd, uploaded_line_count_, 1, 0, 0);
    }
}
