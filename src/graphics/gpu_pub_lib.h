#pragma once

// Public graphics-engine surface (no Vulkan types).
// Implemented by VulkanEngine via IBlockvizEngine (E6).
// See docs/graphics-modularization-design.md

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

// Vulkan-free name + semver (host and engine each own their identity).
// Graphics packs with VK_MAKE_VERSION when building VkApplicationInfo.
struct SoftwareIdentity
{
    const char* name          = "unknown";
    uint32_t    version_major = 0;
    uint32_t    version_minor = 0;
    uint32_t    version_patch = 0;
};

struct EngineCreateInfo
{
    void*    platform_instance = nullptr; // HINSTANCE
    void*    window            = nullptr; // HWND
    uint32_t width             = 0;
    uint32_t height            = 0;
    bool     enable_validation = true;    // Debug default true

    // Host-owned application identity → VkApplicationInfo application fields
    SoftwareIdentity application{};
};

struct CameraUBO
{
    glm::mat4 view{};
    glm::mat4 proj{};
    glm::vec3 light_pos{};
    float     pad1 = 0.0f;
    glm::vec3 view_pos{};
    float     pad2 = 0.0f;
    // Global tween multipliers (1 = identity). Instance scale/alpha multiply these.
    float     anim_scale = 1.0f;
    float     anim_alpha = 1.0f;
    float     anim_time  = 0.0f; // seconds (host/engine may advance)
    float     pad3       = 0.0f;
};

// Instance: world pos + uniform scale + color + alpha (32 bytes).
inline constexpr float kDefaultBlockScale = 1.0f;

struct GpuInstance
{
    glm::vec3 pos{ 0.f };
    float     scale = kDefaultBlockScale; // uniform XYZ scale (mesh verts at ±1)
    glm::vec3 color{ 1.f };
    float     alpha = 1.0f;               // for fade tweens
};

struct FrameSubmit
{
    const GpuInstance* instances      = nullptr;
    size_t             instance_count = 0;
    CameraUBO          camera{};
    uint64_t           client_seq     = 0; // correlates with pick map
};

struct PickQuery
{
    uint32_t mouse_x = 0;
    uint32_t mouse_y = 0;
};

struct PickResult
{
    bool     hit            = false;
    uint32_t instance_index = ~0u;
    uint64_t frame_seq      = 0; // which submitted frame was GPU-visible
};

// App chrome drawn on the render thread between ImGui::NewFrame and ImGui::Render
class IUiOverlay
{
public:
    virtual ~IUiOverlay() = default;
    virtual void draw() = 0; // ImGui::* calls only; no Vulkan
};

class IRenderEngine
{
public:
    virtual ~IRenderEngine() = default;

    virtual void initialize(const EngineCreateInfo& info) = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void shutdown() = 0;

    virtual void start() = 0; // engine-owned render thread
    virtual void stop() = 0;  // joins thread; safe after last submit

    // Thread-safe from main: deep-copy into next publish slot (triple-buffer, latest-wins).
    // Pointers in FrameSubmit need only live for the duration of this call.
    // Engine applies the published slot on the render thread before GPU upload.
    virtual void submit_frame(const FrameSubmit& frame) = 0;

    virtual void set_ui_overlay(IUiOverlay* overlay) = 0; // not owned; nullptr = none

    virtual void request_pick(const PickQuery& q) = 0;
    virtual bool consume_pick(PickResult& out) = 0; // true if a new result is available
};

// Factory (declared only in PR1; implement when engine is peeled from VulkanEngine)
IRenderEngine* create_vulkan_engine();
void destroy_render_engine(IRenderEngine* engine);
