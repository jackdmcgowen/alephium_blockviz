#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "engine_requirements.hpp"
#include "graphics/core/queue_types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
const char* device_type_str(VkPhysicalDeviceType t)
{
    switch (t)
    {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "cpu";
    default:                                    return "unknown";
    }
}

void get_device_uuid(VkPhysicalDevice pd, uint8_t out_uuid[VK_UUID_SIZE])
{
    std::memset(out_uuid, 0, VK_UUID_SIZE);
    VkPhysicalDeviceIDProperties id_props{};
    id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &id_props;
    vkGetPhysicalDeviceProperties2(pd, &props2);
    std::memcpy(out_uuid, id_props.deviceUUID, VK_UUID_SIZE);
}

// Normalize UUID text: strip dashes/spaces, lowercase hex → 32 chars.
std::string normalize_uuid_text(const char* s)
{
    std::string out;
    if (!s)
        return out;
    for (const char* p = s; *p; ++p)
    {
        if (*p == '-' || *p == ' ' || *p == '{' || *p == '}')
            continue;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return out;
}

bool uuid_matches(const uint8_t uuid[VK_UUID_SIZE], const char* want_text)
{
    if (!want_text || !want_text[0])
        return false;
    char hex[33]{};
    format_device_uuid_hex(uuid, hex, sizeof(hex));
    const std::string want = normalize_uuid_text(want_text);
    if (want.size() != 32)
        return false;
    return want == hex;
}

bool name_matches(const char* device_name, const char* want)
{
    if (!want || !want[0] || !device_name)
        return false;
    // case-insensitive substring
    const std::string hay = device_name;
    const std::string needle = want;
    auto lower = [](std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(hay).find(lower(needle)) != std::string::npos;
}
} // namespace

void format_device_uuid_hex(const uint8_t uuid[VK_UUID_SIZE], char* out, size_t out_n)
{
    if (!out || out_n == 0)
        return;
    if (out_n < 33)
    {
        out[0] = '\0';
        return;
    }
    static const char* kHex = "0123456789abcdef";
    for (int i = 0; i < static_cast<int>(VK_UUID_SIZE); ++i)
    {
        out[i * 2]     = kHex[(uuid[i] >> 4) & 0xf];
        out[i * 2 + 1] = kHex[uuid[i] & 0xf];
    }
    out[32] = '\0';
}

uint32_t list_physical_devices(VkInstance instance)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        std::printf("[engine] no physical devices\n");
        return 0;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    const DeviceFeatureRequirements req{};
    std::printf("[engine] physical devices (%u):\n", deviceCount);
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        uint8_t uuid[VK_UUID_SIZE]{};
        get_device_uuid(devices[i], uuid);
        char uuid_hex[33]{};
        format_device_uuid_hex(uuid, uuid_hex, sizeof(uuid_hex));
        char reason[256]{};
        const bool ok = physical_device_meets_requirements(devices[i], req, reason, sizeof(reason));
        std::printf("  [%u] %s  type=%s  api=%u.%u.%u  uuid=%s  %s%s%s\n",
                    i,
                    props.deviceName,
                    device_type_str(props.deviceType),
                    VK_VERSION_MAJOR(props.apiVersion),
                    VK_VERSION_MINOR(props.apiVersion),
                    VK_VERSION_PATCH(props.apiVersion),
                    uuid_hex,
                    ok ? "OK" : "SKIP",
                    ok ? "" : ": ",
                    ok ? "" : reason);
    }
    return deviceCount;
}

VkPhysicalDevice pick_physical_device(
    VkInstance instance,
    VkPhysicalDeviceProperties *device_props,
    VkPhysicalDeviceMemoryProperties *device_mem_props,
    const DevicePickHint* hint)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("No Vulkan-capable devices found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    const DeviceFeatureRequirements req{};
    const bool want_index = hint && hint->index >= 0;
    const bool want_name  = hint && hint->name && hint->name[0];
    const bool want_uuid  = hint && hint->uuid && hint->uuid[0];
    const bool explicit_hint = want_index || want_name || want_uuid;

    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties best_props{};
    VkPhysicalDeviceMemoryProperties best_mem{};
    bool best_is_discrete = false;
    int best_index = -1;
    char reason[256]{};

    // Explicit UUID wins; then name; then index; else discrete preference.
    if (want_uuid)
    {
        for (uint32_t i = 0; i < deviceCount; ++i)
        {
            uint8_t uuid[VK_UUID_SIZE]{};
            get_device_uuid(devices[i], uuid);
            if (!uuid_matches(uuid, hint->uuid))
                continue;
            VkPhysicalDeviceProperties props{};
            VkPhysicalDeviceMemoryProperties mem{};
            vkGetPhysicalDeviceProperties(devices[i], &props);
            vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
            if (!physical_device_meets_requirements(devices[i], req, reason, sizeof(reason)))
            {
                throw std::runtime_error(std::string("Device UUID match '") + props.deviceName +
                                         "' does not meet requirements: " + reason);
            }
            best = devices[i];
            best_props = props;
            best_mem = mem;
            best_index = static_cast<int>(i);
            break;
        }
        if (best == VK_NULL_HANDLE)
            throw std::runtime_error(std::string("No device matches UUID '") + hint->uuid + "'");
    }
    else if (want_name)
    {
        for (uint32_t i = 0; i < deviceCount; ++i)
        {
            VkPhysicalDeviceProperties props{};
            VkPhysicalDeviceMemoryProperties mem{};
            vkGetPhysicalDeviceProperties(devices[i], &props);
            vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
            if (!name_matches(props.deviceName, hint->name))
                continue;
            if (!physical_device_meets_requirements(devices[i], req, reason, sizeof(reason)))
            {
                std::printf("[engine] skip name-match '%s': %s\n", props.deviceName, reason);
                continue;
            }
            // Prefer discrete among name matches; first discrete, else first match.
            const bool discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            if (best == VK_NULL_HANDLE || (discrete && !best_is_discrete))
            {
                best = devices[i];
                best_props = props;
                best_mem = mem;
                best_is_discrete = discrete;
                best_index = static_cast<int>(i);
            }
        }
        if (best == VK_NULL_HANDLE)
            throw std::runtime_error(std::string("No device matches name '") + hint->name + "'");
    }
    else if (want_index)
    {
        if (static_cast<uint32_t>(hint->index) >= deviceCount)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "physical_device_index %d out of range (0..%u)",
                          hint->index, deviceCount ? deviceCount - 1 : 0);
            throw std::runtime_error(buf);
        }
        const uint32_t i = static_cast<uint32_t>(hint->index);
        VkPhysicalDeviceProperties props{};
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);
        if (!physical_device_meets_requirements(devices[i], req, reason, sizeof(reason)))
        {
            throw std::runtime_error(std::string("Device index ") + std::to_string(i) + " '" +
                                     props.deviceName + "' does not meet requirements: " + reason);
        }
        best = devices[i];
        best_props = props;
        best_mem = mem;
        best_index = static_cast<int>(i);
    }
    else
    {
        for (uint32_t i = 0; i < deviceCount; ++i)
        {
            VkPhysicalDeviceProperties props{};
            VkPhysicalDeviceMemoryProperties mem{};
            vkGetPhysicalDeviceProperties(devices[i], &props);
            vkGetPhysicalDeviceMemoryProperties(devices[i], &mem);

            if (!physical_device_meets_requirements(devices[i], req, reason, sizeof(reason)))
            {
                std::printf("[engine] skip device [%u] '%s': %s\n", i, props.deviceName, reason);
                continue;
            }

            const bool discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            if (best == VK_NULL_HANDLE || (discrete && !best_is_discrete))
            {
                best = devices[i];
                best_props = props;
                best_mem = mem;
                best_is_discrete = discrete;
                best_index = static_cast<int>(i);
            }
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        throw std::runtime_error(
            "No Vulkan device meets engine requirements "
            "(API 1.3+, timelineSemaphore, dynamicRendering, synchronization2, VK_KHR_swapchain)");
    }

    uint8_t uuid[VK_UUID_SIZE]{};
    get_device_uuid(best, uuid);
    char uuid_hex[33]{};
    format_device_uuid_hex(uuid, uuid_hex, sizeof(uuid_hex));
    std::printf("[engine] picked device [%d] '%s' type=%s uuid=%s%s\n",
                best_index,
                best_props.deviceName,
                device_type_str(best_props.deviceType),
                uuid_hex,
                explicit_hint ? " (explicit hint)" : " (auto)");

    if (device_props)
        *device_props = best_props;
    if (device_mem_props)
        *device_mem_props = best_mem;

    return best;
}

// surface == VK_NULL_HANDLE: skip WSI present query (headless / NVIDIA-safe).
static void select_queue_families(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                  DeviceQueues& out)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, props.data());

    uint32_t fam_3d = UINT32_MAX;
    uint32_t fam_cmp = UINT32_MAX;
    uint32_t fam_tx = UINT32_MAX;
    uint32_t fam_cmp_fallback = UINT32_MAX;
    uint32_t fam_tx_fallback = UINT32_MAX;
    const bool query_present = (surface != VK_NULL_HANDLE);

    for (uint32_t i = 0; i < count; ++i)
    {
        const VkQueueFlags flags = props[i].queueFlags;
        const bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute  = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool transfer = (flags & VK_QUEUE_TRANSFER_BIT) != 0 || graphics || compute;

        VkBool32 present = VK_FALSE;
        if (query_present)
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        else if (graphics)
            present = VK_TRUE; // headless: any graphics family is fine

        // _3D: graphics + present
        if (graphics && present && fam_3d == UINT32_MAX)
            fam_3d = i;

        // Prefer dedicated compute (compute without graphics)
        if (compute && !graphics && fam_cmp == UINT32_MAX)
            fam_cmp = i;
        if (compute && fam_cmp_fallback == UINT32_MAX)
            fam_cmp_fallback = i;

        // Prefer dedicated transfer (transfer bit, no graphics/compute)
        if ((flags & VK_QUEUE_TRANSFER_BIT) && !graphics && !compute && fam_tx == UINT32_MAX)
            fam_tx = i;
        if (transfer && fam_tx_fallback == UINT32_MAX)
            fam_tx_fallback = i;
    }

    if (fam_3d == UINT32_MAX)
    {
        // Fall back: any graphics family (present may still work on many Win32 drivers)
        for (uint32_t i = 0; i < count; ++i)
        {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                fam_3d = i;
                break;
            }
        }
    }
    if (fam_3d == UINT32_MAX)
        throw std::runtime_error("No graphics queue family found");

    if (fam_cmp == UINT32_MAX)
        fam_cmp = (fam_cmp_fallback != UINT32_MAX) ? fam_cmp_fallback : fam_3d;
    if (fam_tx == UINT32_MAX)
        fam_tx = (fam_tx_fallback != UINT32_MAX) ? fam_tx_fallback : fam_3d;

    out.family[static_cast<uint32_t>(QueueType::_3D)] = fam_3d;
    out.family[static_cast<uint32_t>(QueueType::TX)]  = fam_tx;
    out.family[static_cast<uint32_t>(QueueType::CMP)] = fam_cmp;
    out.dedicated_tx  = (fam_tx != fam_3d);
    out.dedicated_cmp = (fam_cmp != fam_3d);

    std::printf("[engine] queues: _3D family=%u  TX family=%u%s  CMP family=%u%s%s\n",
                fam_3d,
                fam_tx, out.dedicated_tx ? " (dedicated)" : " (shared)",
                fam_cmp, out.dedicated_cmp ? " (dedicated)" : " (shared)",
                query_present ? "" : " (headless: no present query)");
}

void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkSurfaceKHR        surface,
    VkDevice           *device,
    DeviceQueues       *out_queues,
    bool                enable_mesh_shaders,
    bool*               mesh_enabled)
{
    (void)instance;
    if (!device || !out_queues)
        throw std::runtime_error("create_device: null out params");
    if (mesh_enabled)
        *mesh_enabled = false;

    DeviceQueues qinfo{};
    select_queue_families(physicalDevice, surface, qinfo);

    // Unique families for VkDeviceQueueCreateInfo
    uint32_t unique_families[3]{};
    uint32_t unique_count = 0;
    auto add_unique = [&](uint32_t fam) {
        for (uint32_t i = 0; i < unique_count; ++i)
            if (unique_families[i] == fam)
                return;
        unique_families[unique_count++] = fam;
    };
    add_unique(qinfo.family_index(QueueType::_3D));
    add_unique(qinfo.family_index(QueueType::TX));
    add_unique(qinfo.family_index(QueueType::CMP));

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_cis(unique_count);
    for (uint32_t i = 0; i < unique_count; ++i)
    {
        queue_cis[i] = {};
        queue_cis[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_cis[i].queueFamilyIndex = unique_families[i];
        queue_cis[i].queueCount = 1;
        queue_cis[i].pQueuePriorities = &priority;
    }

    DeviceOptionalFeatures opt{};
    query_optional_device_features(physicalDevice, opt);
    const bool arm_mesh = enable_mesh_shaders && opt.mesh_path_usable();

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    // Mesh SPIR-V from glslc may use LocalSizeId; requires maintenance4 (VUID-…-06434).
    vulkan13Features.maintenance4 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;
    vulkan12Features.pNext = &vulkan13Features;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    if (arm_mesh)
    {
        meshFeatures.meshShader = VK_TRUE;
        // Amplification (task) on by default when hardware reports taskShader.
        meshFeatures.taskShader = opt.task_shader ? VK_TRUE : VK_FALSE;
        meshFeatures.pNext = &vulkan12Features;
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queue_cis.data();
    createInfo.queueCreateInfoCount = unique_count;
    createInfo.pEnabledFeatures = &deviceFeatures;

    const char* ext_swapchain = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    const char* ext_mesh = VK_EXT_MESH_SHADER_EXTENSION_NAME;
    const char* extensions[2] = { ext_swapchain, ext_mesh };
    createInfo.enabledExtensionCount = arm_mesh ? 2u : 1u;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.pNext = arm_mesh ? static_cast<void*>(&meshFeatures)
                                : static_cast<void*>(&vulkan12Features);

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, device) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create logical device (features should have been validated at pick)");
    }

    for (uint32_t i = 0; i < queue_type_count(); ++i)
    {
        const QueueType t = static_cast<QueueType>(i);
        vkGetDeviceQueue(*device, qinfo.family_index(t), 0, &qinfo.handle[i]);
    }

    *out_queues = qinfo;
    if (mesh_enabled)
        *mesh_enabled = arm_mesh;
    if (arm_mesh)
    {
        std::printf("[engine] mesh_shaders=enabled (VK_EXT_mesh_shader + meshShader%s)\n",
                    opt.task_shader ? " + taskShader/amplification" : "");
    }
}

void destroy_device(VkDevice device)
{
    vkDestroyDevice(device, nullptr);
}

uint32_t find_device_memory_type(
    VkPhysicalDeviceMemoryProperties* deviceMemProps,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < deviceMemProps->memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (deviceMemProps->memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}
