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
                       uint32_t max_indices)
{
    destroy();

    device_       = device;
    mem_props_    = mem_props;
    color_format_ = color_format;
    depth_format_ = depth_format;
    max_vertices_ = max_vertices;
    max_indices_  = max_indices;

    const VkDeviceSize vbo_size = static_cast<VkDeviceSize>(max_vertices_) * sizeof(DebugVertex);
    const VkDeviceSize ibo_size = static_cast<VkDeviceSize>(max_indices_) * sizeof(uint32_t);

    create_buffer(device_, mem_props_, vbo_size,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  vertex_buffer_, vertex_memory_);

    create_buffer(device_, mem_props_, ibo_size,
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  index_buffer_, index_memory_);

    if (vkMapMemory(device_, vertex_memory_, 0, VK_WHOLE_SIZE, 0, &vertex_mapped_) != VK_SUCCESS)
        return false;
    if (vkMapMemory(device_, index_memory_, 0, VK_WHOLE_SIZE, 0, &index_mapped_) != VK_SUCCESS)
        return false;

    return create_pipeline();
}

void MeshArena::destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    if (pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (vertex_mapped_)
    {
        vkUnmapMemory(device_, vertex_memory_);
        vertex_mapped_ = nullptr;
    }
    if (index_mapped_)
    {
        vkUnmapMemory(device_, index_memory_);
        index_mapped_ = nullptr;
    }

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

    uploaded_index_count_  = 0;
    uploaded_vertex_count_ = 0;
    device_ = VK_NULL_HANDLE;
}

void MeshArena::upload(const DebugDrawer& drawer)
{
    uploaded_index_count_  = 0;
    uploaded_vertex_count_ = 0;

    const uint32_t vc = drawer.vertex_count();
    const uint32_t ic = drawer.index_count();
    if (vc == 0 || ic == 0 || !vertex_mapped_ || !index_mapped_)
        return;

    if (vc > max_vertices_ || ic > max_indices_)
    {
        static bool logged = false;
        if (!logged)
        {
            std::fprintf(stderr, "[MeshArena] debug mesh overflow (v=%u/%u i=%u/%u); skipping\n",
                         vc, max_vertices_, ic, max_indices_);
            logged = true;
        }
        return;
    }

    std::memcpy(vertex_mapped_, drawer.vertices(), vc * sizeof(DebugVertex));
    std::memcpy(index_mapped_, drawer.indices(), ic * sizeof(uint32_t));
    uploaded_vertex_count_ = vc;
    uploaded_index_count_  = ic;
}

bool MeshArena::create_pipeline()
{
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset     = 0;
    push.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push;

    if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

    std::vector<uint8_t> vert_code;
    std::vector<uint8_t> frag_code;
    load_shader_source("debug_mesh_vert.spv", vert_code);
    load_shader_source("color_frag.spv", frag_code);

    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    create_shader_module(device_, vert_module, vert_code);
    create_shader_module(device_, frag_module, frag_code);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(DebugVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribs[2] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(DebugVertex, position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DebugVertex, color) },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions    = attribs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil.depthTestEnable  = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable         = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;

    VkPipelineColorBlendStateCreateInfo color_blending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering_info.colorAttachmentCount    = 1;
    rendering_info.pColorAttachmentFormats = &color_format_;
    rendering_info.depthAttachmentFormat   = depth_format_;

    VkGraphicsPipelineCreateInfo pipeline_info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline_info.pNext               = &rendering_info;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline_layout_;
    pipeline_info.renderPass          = VK_NULL_HANDLE;

    const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);

    vkDestroyShaderModule(device_, vert_module, nullptr);
    vkDestroyShaderModule(device_, frag_module, nullptr);

    return result == VK_SUCCESS;
}

void MeshArena::draw(VkCommandBuffer cmd, const glm::mat4& view_proj)
{
    if (uploaded_index_count_ == 0 || pipeline_ == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &view_proj);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, &offset);
    vkCmdBindIndexBuffer(cmd, index_buffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, uploaded_index_count_, 1, 0, 0, 0);
}
