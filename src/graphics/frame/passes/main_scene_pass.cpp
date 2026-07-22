#include "graphics/pch.h"
#include "graphics/frame/passes/main_scene_pass.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/debug/debug_drawer.h"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/gpu_prv_lib.h"
#include "graphics/mesh_arena.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <stdexcept>
#include <vector>

namespace frame_graph
{

void MainScenePass::create(const PassCreateInfo& info)
{
    create_cube_pipeline_(info);
}

void MainScenePass::destroy(VkDevice device)
{
    destroy_cube_pipeline_(device);
}

void MainScenePass::recreate(const PassCreateInfo& info)
{
    destroy_cube_pipeline_(info.device);
    create_cube_pipeline_(info);
}

void MainScenePass::declare_resources(std::vector<ResourceId>& /*reads*/,
                                      std::vector<ResourceId>& writes) const
{
    writes.push_back(ResourceId::SwapchainColor);
    writes.push_back(ResourceId::SceneDepth);
}

void MainScenePass::create_cube_pipeline_(const PassCreateInfo& info)
{
    destroy_cube_pipeline_(info.device);
    if (!info.device || !info.frame_ubo_layout)
        throw std::runtime_error("MainScenePass: invalid create info");

    std::vector<uint8_t> vertShaderCode;
    std::vector<uint8_t> fragShaderCode;
    load_shader_source("vert.spv", vertShaderCode);
    load_shader_source("frag.spv", fragShaderCode);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    create_shader_module(info.device, vertShaderModule, vertShaderCode);
    create_shader_module(info.device, fragShaderModule, fragShaderCode);

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

    cube_layout_ = create_pipeline_layout(info.device, &info.frame_ubo_layout, 1);

    GraphicsPipelineCreateInfo ginfo{};
    ginfo.layout = cube_layout_;
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
    ginfo.samples = info.samples;
    ginfo.alpha_to_coverage = info.alpha_to_coverage;
    ginfo.color_format = info.color_format;
    ginfo.depth_format = info.depth_format;
    ginfo.color_attachment_count = 1;
    ginfo.viewport_width = info.width;
    ginfo.viewport_height = info.height;
    ginfo.dynamic_viewport_scissor = true;
    ginfo.dynamic_primitive_topology = true;

    try
    {
        cube_pipeline_ = create_graphics_pipeline(info.device, ginfo);
    }
    catch (...)
    {
        destroy_shader_module(info.device, fragShaderModule);
        destroy_shader_module(info.device, vertShaderModule);
        destroy_cube_pipeline_(info.device);
        throw;
    }

    destroy_shader_module(info.device, fragShaderModule);
    destroy_shader_module(info.device, vertShaderModule);
}

void MainScenePass::destroy_cube_pipeline_(VkDevice device)
{
    if (device == VK_NULL_HANDLE)
        return;
    destroy_pipeline(device, cube_pipeline_);
    cube_pipeline_ = VK_NULL_HANDLE;
    destroy_pipeline_layout(device, cube_layout_);
    cube_layout_ = VK_NULL_HANDLE;
}

void MainScenePass::begin_command_buffer(VkCommandBuffer cmd, FrameProfiler* profiler) const
{
    if (!cmd)
        throw std::runtime_error("MainScenePass: null command buffer");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin recording command buffer");
    if (profiler)
        profiler->ensure_pool_reset(cmd);
}

void MainScenePass::end_command_buffer(VkCommandBuffer cmd) const
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}

void MainScenePass::transition_color_to_present(VkCommandBuffer cmd, VkImage color_image) const
{
    cmd_image_barrier(cmd, color_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
}

void MainScenePass::record(const PassRecordParams& p)
{
    if (!p.base.cmd || cube_pipeline_ == VK_NULL_HANDLE)
        throw std::runtime_error("MainScenePass::record: not ready");

    const VkCommandBuffer cmd = p.base.cmd;
    const bool msaa = p.samples > VK_SAMPLE_COUNT_1_BIT && p.resolve_color_view != VK_NULL_HANDLE;

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = p.color_view;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { 0.043f, 0.043f, 0.047f, 1.0f } };
    if (msaa)
    {
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = p.resolve_color_view;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = p.depth_view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { { 0, 0 }, { p.width, p.height } };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    if (p.after_resize || msaa)
    {
        if (msaa && p.color_image != VK_NULL_HANDLE)
        {
            cmd_image_barrier(cmd, p.color_image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
        }
        if (p.resolve_color_image != VK_NULL_HANDLE || (!msaa && p.color_image != VK_NULL_HANDLE))
        {
            VkImage img = msaa ? p.resolve_color_image : p.color_image;
            cmd_image_barrier(cmd, img,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
        }
        cmd_image_barrier(cmd, p.depth_image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });
    }

    {
        FrameProfiler::GpuScope main_gpu(
            p.profiler, cmd, "MainColorDepth",
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);

        vkCmdBeginRendering(cmd, &renderInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipeline_);
        vkCmdSetPrimitiveTopology(cmd, p.topology);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(p.width);
        viewport.height = static_cast<float>(p.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = p.scissor_extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        {
            FrameProfiler::GpuScope cubes_gpu(
                p.profiler, cmd, "Cubes",
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

            VkBuffer buffers[] = { p.vertex_buffer, p.instance_buffer };
            VkDeviceSize offsets[] = { 0, 0 };
            vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
            VkDescriptorSet set = p.frame_ubo_set;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_layout_, 0, 1,
                                    &set, 0, nullptr);
            vkCmdBindIndexBuffer(cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cmd, p.index_count, p.instance_count, 0, 0, 0);
        }

        if (p.mesh_arena && p.debug_drawer && p.view_proj)
        {
            if (p.profiler)
            {
                auto cpu = p.profiler->cpu_scope("MeshArenaUpload");
                p.mesh_arena->upload(*p.debug_drawer);
            }
            else
            {
                p.mesh_arena->upload(*p.debug_drawer);
            }
            FrameProfiler::GpuScope dbg_gpu(
                p.profiler, cmd, "DebugMesh",
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
            p.mesh_arena->draw(cmd, *p.view_proj);
        }

        vkCmdEndRendering(cmd);
    }

    if (p.imgui_draw_data)
    {
        FrameProfiler::GpuScope imgui_gpu(
            p.profiler, cmd, "ImGui",
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(p.width);
        viewport.height = static_cast<float>(p.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = p.scissor_extent;

        VkImageView ui_view = msaa ? p.resolve_color_view : p.color_view;
        VkRenderingAttachmentInfo uiColor{};
        uiColor.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        uiColor.imageView = ui_view;
        uiColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        uiColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        uiColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo uiInfo{};
        uiInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        uiInfo.renderArea = { { 0, 0 }, { p.width, p.height } };
        uiInfo.layerCount = 1;
        uiInfo.colorAttachmentCount = 1;
        uiInfo.pColorAttachments = &uiColor;
        vkCmdBeginRendering(cmd, &uiInfo);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        ImGui_ImplVulkan_RenderDrawData(p.imgui_draw_data, cmd);
        vkCmdEndRendering(cmd);
    }

    if (p.transition_color_to_present)
    {
        VkImage present_img = msaa ? p.resolve_color_image : p.color_image;
        transition_color_to_present(cmd, present_img);
    }
}

} // namespace frame_graph
