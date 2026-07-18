#include "graphics/pch.h"
#include "graphics/pipelines/cube_pipeline.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "gpu_prv_lib.h"

#include <stdexcept>
#include <vector>

void CubePipeline::create(VkDevice device,
                          VkDescriptorSetLayout set_layout,
                          VkFormat color_format,
                          VkFormat depth_format,
                          uint32_t viewport_width,
                          uint32_t viewport_height,
                          VkSampleCountFlagBits samples,
                          bool alpha_to_coverage)
{
    destroy(device);

    std::vector<uint8_t> vertShaderCode;
    std::vector<uint8_t> fragShaderCode;
    load_shader_source("vert.spv", vertShaderCode);
    load_shader_source("frag.spv", fragShaderCode);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    create_shader_module(device, vertShaderModule, vertShaderCode);
    create_shader_module(device, fragShaderModule, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkVertexInputBindingDescription bindingDescriptions[2];
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(VertexNormal);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexNormal, pos.x);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexNormal, normal.x);

    VkVertexInputAttributeDescription instanceAttributes[4];
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(InstanceData, pos);
    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32_SFLOAT;
    instanceAttributes[1].offset = offsetof(InstanceData, scale);
    instanceAttributes[2].binding = 1;
    instanceAttributes[2].location = 4;
    instanceAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[2].offset = offsetof(InstanceData, color);
    instanceAttributes[3].binding = 1;
    instanceAttributes[3].location = 5;
    instanceAttributes[3].format = VK_FORMAT_R32_SFLOAT;
    instanceAttributes[3].offset = offsetof(InstanceData, alpha);

    VkVertexInputAttributeDescription attributes[] = {
        attributeDescriptions[0], attributeDescriptions[1],
        instanceAttributes[0], instanceAttributes[1],
        instanceAttributes[2], instanceAttributes[3]
    };

    layout = create_pipeline_layout(device, &set_layout, 1);

    GraphicsPipelineCreateInfo ginfo{};
    ginfo.layout = layout;
    ginfo.stages = shaderStages;
    ginfo.stage_count = 2;
    ginfo.bindings = bindingDescriptions;
    ginfo.binding_count = 2;
    ginfo.attributes = attributes;
    ginfo.attribute_count = 6;
    ginfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ginfo.cull_mode = VK_CULL_MODE_BACK_BIT;
    ginfo.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ginfo.depth_test = true;
    ginfo.depth_write = true;
    ginfo.depth_compare = VK_COMPARE_OP_LESS;
    ginfo.blend_mode = PipelineBlendMode::Alpha;
    ginfo.samples = samples;
    ginfo.alpha_to_coverage = alpha_to_coverage;
    ginfo.color_format = color_format;
    ginfo.depth_format = depth_format;
    ginfo.color_attachment_count = 1;
    ginfo.viewport_width = viewport_width;
    ginfo.viewport_height = viewport_height;
    ginfo.dynamic_viewport_scissor = true;
    ginfo.dynamic_primitive_topology = true;

    try
    {
        pipeline = create_graphics_pipeline(device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(device, fragShaderModule);
        destroy_shader_module(device, vertShaderModule);
        destroy(device);
        throw;
    }

    destroy_shader_module(device, fragShaderModule);
    destroy_shader_module(device, vertShaderModule);
}

void CubePipeline::destroy(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;
    destroy_pipeline(device, pipeline);
    pipeline = VK_NULL_HANDLE;
    destroy_pipeline_layout(device, layout);
    layout = VK_NULL_HANDLE;
}
