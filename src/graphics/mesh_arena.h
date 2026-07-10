#pragma once

#include <cstdint>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class DebugDrawer;

// Large persistently mapped VBO/IBO for dynamic debug (and future) meshes.
// Frame model: upload full drawer stream each frame; one indexed draw.
class MeshArena
{
public:
    static constexpr uint32_t kDefaultMaxVertices = 1u << 20; // ~1M
    static constexpr uint32_t kDefaultMaxIndices  = 1u << 21;

    MeshArena() = default;
    ~MeshArena();

    MeshArena(const MeshArena&) = delete;
    MeshArena& operator=(const MeshArena&) = delete;

    bool create(VkDevice device,
                VkPhysicalDeviceMemoryProperties* mem_props,
                VkFormat color_format,
                VkFormat depth_format,
                uint32_t max_vertices = kDefaultMaxVertices,
                uint32_t max_indices  = kDefaultMaxIndices);

    void destroy();

    // Memcpy used range from drawer into mapped buffers.
    void upload(const DebugDrawer& drawer);

    // Record draw inside an existing dynamic rendering scope (does not begin/end rendering).
    // Binds pipeline, push viewProj, draws total index_count from last upload.
    void draw(VkCommandBuffer cmd, const glm::mat4& view_proj);

    uint32_t uploaded_index_count() const { return uploaded_index_count_; }

private:
    bool create_pipeline();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties* mem_props_ = nullptr;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;

    uint32_t max_vertices_ = 0;
    uint32_t max_indices_  = 0;
    uint32_t uploaded_index_count_ = 0;
    uint32_t uploaded_vertex_count_ = 0;

    VkBuffer       vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    void*          vertex_mapped_ = nullptr;

    VkBuffer       index_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory index_memory_ = VK_NULL_HANDLE;
    void*          index_mapped_ = nullptr;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_        = VK_NULL_HANDLE;
};
