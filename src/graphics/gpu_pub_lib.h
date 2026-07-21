#pragma once

// Public graphics types (no Vulkan). Used by IGraphicsSystem / IEngine.
// Living docs: docs/layers/graphics.md (map: docs/layers/README.md).
// Historical modularization design: docs/graphics-modularization-design.md

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
    void*    platform_instance = nullptr; // HINSTANCE | display / unused on GLFW
    void*    window            = nullptr; // HWND | GLFWwindow* | headless sentinel
    uint32_t width             = 0;
    uint32_t height            = 0;
    bool     enable_validation = true;    // Debug default true
    // VK_EXT_headless_surface: no OS window; swapchain present is non-display.
    // Requires width/height. Screenshots use GPU readback (not desktop blit).
    bool     headless          = false;

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

// Domain-agnostic Sobel outline: app assigns color; graphics has no role names.
// instance_index indexes this frame's GpuInstance / pick_map list.
struct SobelOutlineInstance
{
    uint32_t  instance_index = ~0u;
    glm::vec4 color{ 1.f, 1.f, 1.f, 1.35f }; // rgb + intensity in a
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

// ---------------------------------------------------------------------------
// Frame profiler snapshot (Vulkan-free). Filled when frame profiler is enabled.
// Consumed by app HUD and vnv/bench — not graphics private headers.
// ---------------------------------------------------------------------------

enum class FrameBoundClass : uint8_t
{
    Unknown = 0,
    Cpu,
    Gpu,
    PresentSync,
};

inline constexpr uint32_t kMaxFrameTimingScopes = 24;
inline constexpr uint32_t kFrameTimingNameMax   = 32;

struct FrameTimingScope
{
    char  name[kFrameTimingNameMax]{};
    float cpu_ms = 0.f;         // last sample
    float gpu_ms = 0.f;         // last sample
    float cpu_median_ms = 0.f;  // rolling ring
    float gpu_median_ms = 0.f;
    float cpu_p95_ms = 0.f;
    float gpu_p95_ms = 0.f;
};

struct FrameTimingSnapshot
{
    float          frame_ms = 0.f; // host wall for frame
    float          cpu_ms   = 0.f; // sum of exclusive CPU scopes (last)
    float          gpu_ms   = 0.f; // max GPU scope (last; multi-queue safe)
    FrameBoundClass bound    = FrameBoundClass::Unknown;
    uint32_t       scope_count = 0;
    FrameTimingScope scopes[kMaxFrameTimingScopes]{};
    uint64_t       sample_index = 0;
    bool           valid = false;
};

// Render lifecycle types used by IGraphicsSystem / IEngine (see engine/engine.hpp).
