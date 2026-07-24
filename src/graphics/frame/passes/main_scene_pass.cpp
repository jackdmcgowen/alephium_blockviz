#include "graphics/pch.h"
#include "graphics/frame/passes/main_scene_pass.hpp"
#include "graphics/frame/vertex_types.hpp"
#include "graphics/debug/debug_drawer.h"
#include "graphics/frame/profiling/frame_profiler.hpp"
#include "graphics/gpu_prv_lib.h"
#include "graphics/mesh_arena.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace frame_graph
{
namespace
{
struct MeshOnlyPC
{
    uint32_t host_count = 0;
    uint32_t use_gpu_cull = 0;
};

// Must match cube.task.glsl push constant layout.
struct TaskCubePC
{
    float planes[6][4];
    uint32_t instance_count = 0;
    float half_extent = 1.05f;
    float pad0 = 0.f;
    float pad1 = 0.f;
};
} // namespace

void MainScenePass::create(const PassCreateInfo& info)
{
    device_ = info.device;
    want_task_ = info.enable_mesh_task;
    create_cube_pipeline_(info);
    if (info.enable_mesh_cube)
        create_mesh_pipelines_(info);
}

void MainScenePass::destroy(VkDevice device)
{
    destroy_mesh_pipelines_(device);
    destroy_cube_pipeline_(device);
    device_ = VK_NULL_HANDLE;
    want_task_ = false;
}

void MainScenePass::recreate(const PassCreateInfo& info)
{
    destroy_mesh_pipelines_(info.device);
    destroy_cube_pipeline_(info.device);
    device_ = info.device;
    want_task_ = info.enable_mesh_task;
    create_cube_pipeline_(info);
    if (info.enable_mesh_cube)
        create_mesh_pipelines_(info);
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

void MainScenePass::create_mesh_pipelines_(const PassCreateInfo& info)
{
    destroy_mesh_pipelines_(info.device);
    if (!info.device || !info.frame_ubo_buffer || info.frame_ubo_range == 0)
        throw std::runtime_error("MainScenePass mesh: need frame UBO buffer");

    pfn_draw_mesh_tasks_ = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
        vkGetDeviceProcAddr(info.device, "vkCmdDrawMeshTasksEXT"));
    if (!pfn_draw_mesh_tasks_)
        throw std::runtime_error("MainScenePass: vkCmdDrawMeshTasksEXT missing");

    mesh_ubo_buffer_ = info.frame_ubo_buffer;
    std::vector<uint8_t> frag_code;
    load_shader_source("frag.spv", frag_code);

    // --- Mesh-only (compute frustum) ---
    {
        const DescriptorBinding binds[] = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
              VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT },
        };
        mesh_only_set_layout_ = create_descriptor_set_layout(info.device, binds, 3);
        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 },
        };
        mesh_only_pool_ = create_descriptor_pool(info.device, 1, sizes, 2);
        if (!allocate_descriptor_sets(info.device, mesh_only_pool_, &mesh_only_set_layout_, 1,
                                      &mesh_only_set_))
            throw std::runtime_error("MainScenePass mesh-only: allocate set failed");

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
        pcr.offset = 0;
        pcr.size = sizeof(MeshOnlyPC);
        mesh_only_layout_ =
            create_pipeline_layout(info.device, &mesh_only_set_layout_, 1, &pcr, 1);

        std::vector<uint8_t> mesh_code;
        load_shader_source("cube_mesh_only.mesh.spv", mesh_code);
        VkShaderModule mesh_mod = VK_NULL_HANDLE;
        VkShaderModule frag_mod = VK_NULL_HANDLE;
        create_shader_module(info.device, mesh_mod, mesh_code);
        create_shader_module(info.device, frag_mod, frag_code);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        stages[0].module = mesh_mod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag_mod;
        stages[1].pName = "main";

        GraphicsPipelineCreateInfo ginfo{};
        ginfo.layout = mesh_only_layout_;
        ginfo.stages = stages;
        ginfo.stage_count = 2;
        ginfo.mesh_shading = true;
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

        try
        {
            mesh_only_pipeline_ = create_graphics_pipeline(info.device, ginfo);
        }
        catch (...)
        {
            destroy_shader_module(info.device, frag_mod);
            destroy_shader_module(info.device, mesh_mod);
            destroy_mesh_pipelines_(info.device);
            throw;
        }
        destroy_shader_module(info.device, frag_mod);
        destroy_shader_module(info.device, mesh_mod);

        const DescriptorBufferWrite ubo_w{
            mesh_only_set_, 0, info.frame_ubo_buffer, 0, info.frame_ubo_range,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        };
        write_descriptor_buffers(info.device, &ubo_w, 1);
    }

    // --- Task + mesh (amplification frustum + face cone) ---
    if (want_task_)
    {
        const DescriptorBinding binds[] = {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
              VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                  VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
              VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT },
        };
        task_mesh_set_layout_ = create_descriptor_set_layout(info.device, binds, 2);
        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
        };
        task_mesh_pool_ = create_descriptor_pool(info.device, 1, sizes, 2);
        if (!allocate_descriptor_sets(info.device, task_mesh_pool_, &task_mesh_set_layout_, 1,
                                      &task_mesh_set_))
            throw std::runtime_error("MainScenePass task+mesh: allocate set failed");

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT;
        pcr.offset = 0;
        pcr.size = sizeof(TaskCubePC);
        task_mesh_layout_ =
            create_pipeline_layout(info.device, &task_mesh_set_layout_, 1, &pcr, 1);

        std::vector<uint8_t> task_code;
        std::vector<uint8_t> mesh_code;
        load_shader_source("cube.task.spv", task_code);
        load_shader_source("cube.mesh.spv", mesh_code);
        VkShaderModule task_mod = VK_NULL_HANDLE;
        VkShaderModule mesh_mod = VK_NULL_HANDLE;
        VkShaderModule frag_mod = VK_NULL_HANDLE;
        create_shader_module(info.device, task_mod, task_code);
        create_shader_module(info.device, mesh_mod, mesh_code);
        create_shader_module(info.device, frag_mod, frag_code);

        VkPipelineShaderStageCreateInfo stages[3]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        stages[0].module = task_mod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        stages[1].module = mesh_mod;
        stages[1].pName = "main";
        stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[2].module = frag_mod;
        stages[2].pName = "main";

        GraphicsPipelineCreateInfo ginfo{};
        ginfo.layout = task_mesh_layout_;
        ginfo.stages = stages;
        ginfo.stage_count = 3;
        ginfo.mesh_shading = true;
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

        try
        {
            task_mesh_pipeline_ = create_graphics_pipeline(info.device, ginfo);
        }
        catch (...)
        {
            destroy_shader_module(info.device, frag_mod);
            destroy_shader_module(info.device, mesh_mod);
            destroy_shader_module(info.device, task_mod);
            destroy_mesh_pipelines_(info.device);
            throw;
        }
        destroy_shader_module(info.device, frag_mod);
        destroy_shader_module(info.device, mesh_mod);
        destroy_shader_module(info.device, task_mod);

        const DescriptorBufferWrite ubo_w{
            task_mesh_set_, 0, info.frame_ubo_buffer, 0, info.frame_ubo_range,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        };
        write_descriptor_buffers(info.device, &ubo_w, 1);
        std::printf("[engine] cube path: task+mesh (amplification frustum) ready\n");
    }
    else
    {
        std::printf("[engine] cube path: mesh-only (compute frustum) ready\n");
    }
}

void MainScenePass::destroy_mesh_pipelines_(VkDevice device)
{
    VkDevice dev = device ? device : device_;
    if (dev == VK_NULL_HANDLE)
        return;

    destroy_pipeline(dev, task_mesh_pipeline_);
    task_mesh_pipeline_ = VK_NULL_HANDLE;
    destroy_pipeline_layout(dev, task_mesh_layout_);
    task_mesh_layout_ = VK_NULL_HANDLE;
    if (task_mesh_pool_)
    {
        destroy_descriptor_pool(dev, task_mesh_pool_);
        task_mesh_pool_ = VK_NULL_HANDLE;
        task_mesh_set_ = VK_NULL_HANDLE;
    }
    if (task_mesh_set_layout_)
    {
        destroy_descriptor_set_layout(dev, task_mesh_set_layout_);
        task_mesh_set_layout_ = VK_NULL_HANDLE;
    }
    task_mesh_bound_inst_ = VK_NULL_HANDLE;

    destroy_pipeline(dev, mesh_only_pipeline_);
    mesh_only_pipeline_ = VK_NULL_HANDLE;
    destroy_pipeline_layout(dev, mesh_only_layout_);
    mesh_only_layout_ = VK_NULL_HANDLE;
    if (mesh_only_pool_)
    {
        destroy_descriptor_pool(dev, mesh_only_pool_);
        mesh_only_pool_ = VK_NULL_HANDLE;
        mesh_only_set_ = VK_NULL_HANDLE;
    }
    if (mesh_only_set_layout_)
    {
        destroy_descriptor_set_layout(dev, mesh_only_set_layout_);
        mesh_only_set_layout_ = VK_NULL_HANDLE;
    }
    mesh_only_bound_inst_ = VK_NULL_HANDLE;
    mesh_only_bound_draw_ = VK_NULL_HANDLE;
    mesh_ubo_buffer_ = VK_NULL_HANDLE;
    pfn_draw_mesh_tasks_ = nullptr;
}

void MainScenePass::write_mesh_only_descriptors_(VkBuffer instances, VkBuffer draw_args)
{
    if (!device_ || !mesh_only_set_ || !instances || !draw_args)
        return;
    if (instances == mesh_only_bound_inst_ && draw_args == mesh_only_bound_draw_)
        return;
    const DescriptorBufferWrite writes[] = {
        { mesh_only_set_, 1, instances, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
        { mesh_only_set_, 2, draw_args, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
    };
    write_descriptor_buffers(device_, writes, 2);
    mesh_only_bound_inst_ = instances;
    mesh_only_bound_draw_ = draw_args;
}

void MainScenePass::write_task_mesh_descriptors_(VkBuffer instances)
{
    if (!device_ || !task_mesh_set_ || !instances)
        return;
    if (instances == task_mesh_bound_inst_)
        return;
    const DescriptorBufferWrite w{
        task_mesh_set_, 1, instances, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    write_descriptor_buffers(device_, &w, 1);
    task_mesh_bound_inst_ = instances;
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

    PassProfileScope profile(*this, p);
    const VkCommandBuffer cmd = p.base.cmd;
    const bool msaa = p.samples > VK_SAMPLE_COUNT_1_BIT && p.resolve_color_view != VK_NULL_HANDLE;
    const bool use_task =
        p.use_task_mesh_path && task_mesh_ready() && p.instance_count > 0;
    const bool use_mesh_only =
        !use_task && p.use_mesh_cube_path && mesh_only_pipeline_ != VK_NULL_HANDLE &&
        pfn_draw_mesh_tasks_ && p.instance_count > 0;

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
        vkCmdBeginRendering(cmd, &renderInfo);

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

        if (use_task)
        {
            FrameProfiler::GpuScope cubes_gpu(
                p.profiler, cmd, "CubesTaskMesh",
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

            write_task_mesh_descriptors_(p.instance_buffer);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, task_mesh_pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, task_mesh_layout_, 0, 1,
                                    &task_mesh_set_, 0, nullptr);

            TaskCubePC pc{};
            for (int i = 0; i < 6; ++i)
            {
                pc.planes[i][0] = p.cull_planes[i].x;
                pc.planes[i][1] = p.cull_planes[i].y;
                pc.planes[i][2] = p.cull_planes[i].z;
                pc.planes[i][3] = p.cull_planes[i].w;
            }
            pc.instance_count = p.instance_count;
            pc.half_extent = p.cull_half_extent > 0.f ? p.cull_half_extent : 1.05f;
            vkCmdPushConstants(cmd, task_mesh_layout_, VK_SHADER_STAGE_TASK_BIT_EXT, 0, sizeof(pc),
                               &pc);

            const uint32_t task_groups = (p.instance_count + 31u) / 32u;
            pfn_draw_mesh_tasks_(cmd, task_groups, 1, 1);
        }
        else if (use_mesh_only)
        {
            FrameProfiler::GpuScope cubes_gpu(
                p.profiler, cmd, "CubesMesh",
                VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

            const bool cull = p.use_gpu_instance_cull && p.visible_instance_buffer &&
                              p.cull_draw_args_buffer;
            VkBuffer inst_ssbo = cull ? p.visible_instance_buffer : p.instance_buffer;
            VkBuffer draw_ssbo =
                p.cull_draw_args_buffer ? p.cull_draw_args_buffer : p.instance_buffer;
            write_mesh_only_descriptors_(inst_ssbo, draw_ssbo);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_only_pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_only_layout_, 0, 1,
                                    &mesh_only_set_, 0, nullptr);

            MeshOnlyPC pc{};
            pc.host_count = p.instance_count;
            pc.use_gpu_cull = cull ? 1u : 0u;
            vkCmdPushConstants(cmd, mesh_only_layout_, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(pc),
                               &pc);
            pfn_draw_mesh_tasks_(cmd, p.instance_count, 1, 1);
        }
        else
        {
            FrameProfiler::GpuScope cubes_gpu(
                p.profiler, cmd, "CubesClassic",
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_pipeline_);
            vkCmdSetPrimitiveTopology(cmd, p.topology);

            const bool cull = p.use_gpu_instance_cull && p.visible_instance_buffer &&
                              p.cull_draw_args_buffer && p.instance_count > 0;
            VkBuffer inst_vb =
                cull ? p.visible_instance_buffer : p.instance_buffer;
            VkBuffer buffers[] = { p.vertex_buffer, inst_vb };
            VkDeviceSize offsets[] = { 0, 0 };
            vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
            VkDescriptorSet set = p.frame_ubo_set;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cube_layout_, 0, 1,
                                    &set, 0, nullptr);
            vkCmdBindIndexBuffer(cmd, p.index_buffer, 0, VK_INDEX_TYPE_UINT16);
            if (cull)
            {
                vkCmdDrawIndexedIndirect(cmd, p.cull_draw_args_buffer, 0, 1,
                                         sizeof(VkDrawIndexedIndirectCommand));
            }
            else if (p.instance_count > 0)
            {
                vkCmdDrawIndexed(cmd, p.index_count, p.instance_count, 0, 0, 0);
            }
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
