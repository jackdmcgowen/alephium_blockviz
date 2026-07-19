#include "graphics/pch.h"
#include "gpu_prv_lib.h"

#include <array>
#include <stdexcept>
#include <vector>

VkPipelineLayout create_pipeline_layout(
    VkDevice device,
    const VkDescriptorSetLayout* set_layouts,
    uint32_t set_layout_count,
    const VkPushConstantRange* push_ranges,
    uint32_t push_count)
{
    VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    info.setLayoutCount = set_layout_count;
    info.pSetLayouts = set_layouts;
    info.pushConstantRangeCount = push_count;
    info.pPushConstantRanges = push_ranges;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
        throw std::runtime_error("create_pipeline_layout failed");
    return layout;
}

void destroy_pipeline_layout(VkDevice device, VkPipelineLayout layout)
{
    if (device != VK_NULL_HANDLE && layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, layout, nullptr);
}

void destroy_pipeline(VkDevice device, VkPipeline pipeline)
{
    if (device != VK_NULL_HANDLE && pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, pipeline, nullptr);
}

static VkPipeline create_graphics_pipeline_impl(VkDevice device,
                                                 const GraphicsPipelineCreateInfo& info)
{
    if (device == VK_NULL_HANDLE || info.layout == VK_NULL_HANDLE || !info.stages ||
        info.stage_count == 0)
        throw std::runtime_error("create_graphics_pipeline: invalid args");

    VkPipelineVertexInputStateCreateInfo vertex_input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex_input.vertexBindingDescriptionCount = info.binding_count;
    vertex_input.pVertexBindingDescriptions = info.bindings;
    vertex_input.vertexAttributeDescriptionCount = info.attribute_count;
    vertex_input.pVertexAttributeDescriptions = info.attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = info.topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(info.viewport_width ? info.viewport_width : 1);
    viewport.height = static_cast<float>(info.viewport_height ? info.viewport_height : 1);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = {
        info.viewport_width ? info.viewport_width : 1u,
        info.viewport_height ? info.viewport_height : 1u
    };

    VkPipelineViewportStateCreateInfo viewport_state{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = info.line_width;
    rasterizer.cullMode = info.cull_mode;
    rasterizer.frontFace = info.front_face;

    VkPipelineMultisampleStateCreateInfo multisampling{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = info.samples;
    multisampling.alphaToCoverageEnable = info.alpha_to_coverage ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth_stencil.depthTestEnable = info.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = info.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = info.depth_compare;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = info.color_write_mask;
    color_blend_attachment.blendEnable = VK_FALSE;
    if (info.blend_mode == PipelineBlendMode::Alpha)
    {
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    }
    else if (info.blend_mode == PipelineBlendMode::Additive)
    {
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    else if (info.blend_mode == PipelineBlendMode::Premultiplied)
    {
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }

    VkPipelineColorBlendStateCreateInfo color_blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount =
        info.color_attachment_count > 0 ? 1u : 0u;
    color_blending.pAttachments =
        info.color_attachment_count > 0 ? &color_blend_attachment : nullptr;

    std::array<VkDynamicState, 3> dyn_states{};
    uint32_t dyn_count = 0;
    if (info.dynamic_primitive_topology)
        dyn_states[dyn_count++] = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
    if (info.dynamic_viewport_scissor)
    {
        dyn_states[dyn_count++] = VK_DYNAMIC_STATE_VIEWPORT;
        dyn_states[dyn_count++] = VK_DYNAMIC_STATE_SCISSOR;
    }

    VkPipelineDynamicStateCreateInfo dynamic_state{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = dyn_count;
    dynamic_state.pDynamicStates = dyn_count ? dyn_states.data() : nullptr;

    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = info.color_attachment_count;
    rendering.pColorAttachmentFormats =
        info.color_attachment_count > 0 ? &info.color_format : nullptr;
    rendering.depthAttachmentFormat = info.depth_format;

    VkGraphicsPipelineCreateInfo pipeline_info{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipeline_info.pNext = &rendering;
    pipeline_info.stageCount = info.stage_count;
    pipeline_info.pStages = info.stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = dyn_count ? &dynamic_state : nullptr;
    pipeline_info.layout = info.layout;
    pipeline_info.renderPass = VK_NULL_HANDLE;
    pipeline_info.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                  &pipeline) != VK_SUCCESS)
        throw std::runtime_error("create_graphics_pipeline failed");
    return pipeline;
}

static VkPipeline create_compute_pipeline_from_module_impl(VkDevice device,
                                                           VkPipelineLayout layout,
                                                           VkShaderModule module,
                                                           const char* entry)
{
    if (device == VK_NULL_HANDLE || layout == VK_NULL_HANDLE || module == VK_NULL_HANDLE)
        throw std::runtime_error("create_compute_pipeline_from_module: invalid args");

    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = entry ? entry : "main";

    VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    ci.stage = stage;
    ci.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) !=
        VK_SUCCESS)
        throw std::runtime_error("create_compute_pipeline_from_module failed");
    return pipeline;
}

VkPipeline create_pipeline(VkDevice device, const PipelineCreateInfo& info)
{
    if (device == VK_NULL_HANDLE || info.layout == VK_NULL_HANDLE)
        throw std::runtime_error("create_pipeline: invalid device/layout");

    if (info.kind == PipelineKind::_3D)
    {
        if (!info.graphics)
            throw std::runtime_error("create_pipeline: Graphics3D requires graphics info");
        GraphicsPipelineCreateInfo g = *info.graphics;
        if (g.layout == VK_NULL_HANDLE)
            g.layout = info.layout;
        return create_graphics_pipeline_impl(device, g);
    }

    if (info.kind == PipelineKind::CMP)
    {
        if (info.compute_module != VK_NULL_HANDLE)
        {
            return create_compute_pipeline_from_module_impl(
                device, info.layout, info.compute_module, info.compute_entry);
        }
        if (!info.compute_spv_path)
            throw std::runtime_error("create_pipeline: CMP needs module or spv path");

        std::vector<uint8_t> code;
        load_shader_source(info.compute_spv_path, code);
        VkShaderModule module = VK_NULL_HANDLE;
        create_shader_module(device, module, code);
        VkPipeline pipeline = VK_NULL_HANDLE;
        try
        {
            pipeline = create_compute_pipeline_from_module_impl(
                device, info.layout, module, info.compute_entry);
        }
        catch (...)
        {
            destroy_shader_module(device, module);
            throw;
        }
        destroy_shader_module(device, module);
        return pipeline;
    }

    throw std::runtime_error("create_pipeline: unknown PipelineKind");
}

VkPipeline create_graphics_pipeline(VkDevice device, const GraphicsPipelineCreateInfo& info)
{
    PipelineCreateInfo pci{};
    pci.kind = PipelineKind::_3D;
    pci.layout = info.layout;
    pci.graphics = &info;
    return create_pipeline(device, pci);
}

VkPipeline create_compute_pipeline_from_module(VkDevice device, VkPipelineLayout layout,
                                               VkShaderModule module, const char* entry)
{
    PipelineCreateInfo pci{};
    pci.kind = PipelineKind::CMP;
    pci.layout = layout;
    pci.compute_module = module;
    pci.compute_entry = entry ? entry : "main";
    return create_pipeline(device, pci);
}

VkPipeline create_compute_pipeline(VkDevice device, VkPipelineLayout layout,
                                   const char* shader_spv_path, const char* entry)
{
    PipelineCreateInfo pci{};
    pci.kind = PipelineKind::CMP;
    pci.layout = layout;
    pci.compute_spv_path = shader_spv_path;
    pci.compute_entry = entry ? entry : "main";
    return create_pipeline(device, pci);
}
