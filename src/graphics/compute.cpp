#include "graphics/pch.h"
#include "gpu_prv_lib.h"

#include <stdexcept>
#include <vector>

VkPipelineLayout create_compute_pipeline_layout(
    VkDevice device,
    const VkDescriptorSetLayout* set_layouts,
    uint32_t set_layout_count,
    const VkPushConstantRange* push_ranges,
    uint32_t push_count)
{
    // Same layout path as graphics; separate name for call-site clarity.
    return create_pipeline_layout(device, set_layouts, set_layout_count, push_ranges,
                                  push_count);
}

VkPipeline create_compute_pipeline_from_module(
    VkDevice device,
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

VkPipeline create_compute_pipeline(
    VkDevice device,
    VkPipelineLayout layout,
    const char* shader_spv_path,
    const char* entry)
{
    if (!shader_spv_path)
        throw std::runtime_error("create_compute_pipeline: null shader path");

    std::vector<uint8_t> code;
    load_shader_source(shader_spv_path, code);

    VkShaderModule module = VK_NULL_HANDLE;
    create_shader_module(device, module, code);

    VkPipeline pipeline = VK_NULL_HANDLE;
    try
    {
        pipeline = create_compute_pipeline_from_module(device, layout, module, entry);
    }
    catch (...)
    {
        destroy_shader_module(device, module);
        throw;
    }
    destroy_shader_module(device, module);
    return pipeline;
}
