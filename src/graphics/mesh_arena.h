#pragma once

#include "buffer_manager.hpp"

#include <cstdint>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class DebugDrawer;

// Persistently mapped debug geometry: triangle meshes + LINE_LIST edges (wire boxes).
class MeshArena
{
public:
    static constexpr uint32_t kDefaultMaxVertices = 1u << 20;
    static constexpr uint32_t kDefaultMaxIndices  = 1u << 21;
    static constexpr uint32_t kDefaultMaxLineVerts = 1u << 18;

    MeshArena() = default;
    ~MeshArena();

    MeshArena(const MeshArena&) = delete;
    MeshArena& operator=(const MeshArena&) = delete;

    bool create(VkDevice device,
                VkPhysicalDeviceMemoryProperties* mem_props,
                BufferManager* buffer_manager,
                VkFormat color_format,
                VkFormat depth_format,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                uint32_t max_vertices = kDefaultMaxVertices,
                uint32_t max_indices  = kDefaultMaxIndices,
                uint32_t max_line_verts = kDefaultMaxLineVerts);

    void destroy();

    void upload(const DebugDrawer& drawer);

    // Draw triangles then lines inside an existing dynamic rendering scope.
    void draw(VkCommandBuffer cmd, const glm::mat4& view_proj);

private:
    bool create_pipelines();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props_ = nullptr;
    BufferManager* buffers_ = nullptr;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    uint32_t max_vertices_ = 0;
    uint32_t max_indices_  = 0;
    uint32_t max_line_verts_ = 0;
    uint32_t uploaded_index_count_ = 0;
    uint32_t uploaded_vertex_count_ = 0;
    uint32_t uploaded_line_count_ = 0;

    GpuBuffer vertex_;
    GpuBuffer index_;
    GpuBuffer line_;
    void* vertex_mapped_ = nullptr;
    void* index_mapped_ = nullptr;
    void* line_mapped_ = nullptr;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       tri_pipeline_  = VK_NULL_HANDLE;
    VkPipeline       line_pipeline_ = VK_NULL_HANDLE;
};
