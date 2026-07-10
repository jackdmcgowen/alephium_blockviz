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
                       VkFormat color_format,
                       VkFormat depth_format,
                       uint32_t max_vertices,
                       uint32_t max_indices,
                       uint32_t max_line_verts)
{
    destroy();

    device_       = device;
    mem_props_    = mem_props;
    color_format_ = color_format;
    depth_format_ = depth_format;
    max_vertices_ = max_vertices;
    max_indices_  = max_indices;
    max_line_verts_ = max_line_verts;

    create_buffer(device_, mem_props_, max_vertices_ * sizeof(DebugVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  vertex_buffer_, vertex_memory_);

    create_buffer(device_, mem_props_, max_indices_ * sizeof(uint32_t),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  index_buffer_, index_memory_);

    create_buffer(device_, mem_props_, max_line_verts_ * sizeof(DebugVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  line_buffer_, line_memory_);

    if (vkMapMemory(device_, vertex_memory_, 0, VK_WHOLE_SIZE, 0, &vertex_mapped_) != VK_SUCCESS)
        return false;
    if (vkMapMemory(device_, index_memory_, 0, VK_WHOLE_SIZE, 0, &index_mapped_) != VK_SUCCESS)
        return false;
    if (vkMapMemory(device_, line_memory_, 0, VK_WHOLE_SIZE, 0, &line_mapped_) != VK_SUCCESS)
        return false;

    return create_pipelines();
}

void MeshArena::destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    if (tri_pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, tri_pipeline_, nullptr);
        tri_pipeline_ = VK_NULL_HANDLE;
    }
    if (line_pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, line_pipeline_, nullptr);
        line_pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    auto unmap = [&](void*& p, VkDeviceMemory m) {
        if (p) { vkUnmapMemory(device_, m); p = nullptr; }
    };
    unmap(vertex_mapped_, vertex_memory_);
    unmap(index_mapped_, index_memory_);
    unmap(line_mapped_, line_memory_);

    if (vertex_buffer_ != VK_NULL_HANDLE)
    {
        destroy_buffer(device_, vertex_buffer_, vertex_memory_);
        vertex_buffer_ = VK_NULL_HANDLE;
        vertex_memory_ = VK_NULL_HANDLE;
    }
    if (index_buffer_ != VK_NULL_HANDLE)
    {
        destroy_buffer(device_, index_buffer_, index_memory_);
        index_buffer_ = VK_NULL_HANDLE;
        index_memory_ = VK_NULL_HANDLE;
    }
    if (line_buffer_ != VK_NULL_HANDLE)
    {
        destroy_buffer(device_, line_buffer_, line_memory_);
        line_buffer_ = VK_NULL_HANDLE;
        line_memory_ = VK_NULL_HANDLE;
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
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

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

    VkPipelineVertexInputStateCreateInfo vertex_input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attribs;

    VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    VkPipelineColorBlendStateCreateInfo color_blending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &color_format_;
    rendering_info.depthAttachmentFormat = depth_format_;

    auto make_pipe = [&](VkPrimitiveTopology topo, VkCullModeFlags cull, VkPipeline* out) -> bool {
        VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = topo;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = cull;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkGraphicsPipelineCreateInfo pipeline_info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_info.pNext = &rendering_info;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout_;
        pipeline_info.renderPass = VK_NULL_HANDLE;

        return vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, out) == VK_SUCCESS;
    };

    const bool ok_tri = make_pipe(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_BACK_BIT, &tri_pipeline_);
    const bool ok_line = make_pipe(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_CULL_MODE_NONE, &line_pipeline_);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);

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
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, &offset);
        vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, uploaded_index_count_, 1, 0, 0, 0);
    }

    if (uploaded_line_count_ > 0 && line_pipeline_ != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &view_proj);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &line_buffer_, &offset);
        vkCmdDraw(cmd, uploaded_line_count_, 1, 0, 0);
    }
}
