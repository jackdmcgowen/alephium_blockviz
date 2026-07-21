#include "graphics/platform/gfx_platform.hpp"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal PNG writer (RGBA8). No external encode dep on Linux path.
namespace
{
void write_be32(std::vector<unsigned char>& out, uint32_t v)
{
    out.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(v & 0xff));
}

uint32_t crc32_ieee(const unsigned char* data, size_t len)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void append_chunk(std::vector<unsigned char>& png,
                  const char type[4],
                  const unsigned char* data,
                  size_t len)
{
    write_be32(png, static_cast<uint32_t>(len));
    const size_t type_off = png.size();
    png.insert(png.end(), type, type + 4);
    if (len && data)
        png.insert(png.end(), data, data + len);
    const uint32_t crc = crc32_ieee(png.data() + type_off, 4 + len);
    write_be32(png, crc);
}

// Store uncompressed deflate blocks (zlib wrapper) — large but simple.
bool write_png_rgba(const char* path, int w, int h, const unsigned char* rgba)
{
    if (!path || w < 1 || h < 1 || !rgba)
        return false;

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4));
    for (int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter None
        const unsigned char* row = rgba + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
    }

    // zlib stream: CMF/FLG + raw deflate stored blocks + adler32
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size())
    {
        size_t chunk = raw.size() - pos;
        if (chunk > 65535)
            chunk = 65535;
        const bool last = (pos + chunk) >= raw.size();
        z.push_back(last ? 0x01 : 0x00);
        const uint16_t n = static_cast<uint16_t>(chunk);
        const uint16_t ninv = static_cast<uint16_t>(~n);
        z.push_back(static_cast<unsigned char>(n & 0xff));
        z.push_back(static_cast<unsigned char>((n >> 8) & 0xff));
        z.push_back(static_cast<unsigned char>(ninv & 0xff));
        z.push_back(static_cast<unsigned char>((ninv >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;
    }
    // Adler-32 of raw
    uint32_t s1 = 1, s2 = 0;
    for (unsigned char b : raw)
    {
        s1 = (s1 + b) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    const uint32_t adler = (s2 << 16) | s1;
    write_be32(z, adler);

    std::vector<unsigned char> png;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    png.insert(png.end(), sig, sig + 8);

    unsigned char ihdr[13];
    ihdr[0] = static_cast<unsigned char>((w >> 24) & 0xff);
    ihdr[1] = static_cast<unsigned char>((w >> 16) & 0xff);
    ihdr[2] = static_cast<unsigned char>((w >> 8) & 0xff);
    ihdr[3] = static_cast<unsigned char>(w & 0xff);
    ihdr[4] = static_cast<unsigned char>((h >> 24) & 0xff);
    ihdr[5] = static_cast<unsigned char>((h >> 16) & 0xff);
    ihdr[6] = static_cast<unsigned char>((h >> 8) & 0xff);
    ihdr[7] = static_cast<unsigned char>(h & 0xff);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // RGBA
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    append_chunk(png, "IHDR", ihdr, 13);
    append_chunk(png, "IDAT", z.data(), z.size());
    append_chunk(png, "IEND", nullptr, 0);

    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    const size_t n = std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return n == png.size();
}
} // namespace

uint32_t gfx_platform_surface_extension_names(const char** names, uint32_t capacity)
{
    if (!names || capacity == 0)
        return 0;
    uint32_t count = 0;
    uint32_t glfw_count = 0;
    const char** glfw_ext = glfwGetRequiredInstanceExtensions(&glfw_count);
    if (!glfw_ext)
    {
        // Fallback: surface only (will fail later without platform ext).
        if (count < capacity)
            names[count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        return count;
    }
    for (uint32_t i = 0; i < glfw_count && count < capacity; ++i)
        names[count++] = glfw_ext[i];
    return count;
}

VkSurfaceKHR gfx_platform_create_surface(VkInstance instance,
                                         void* window,
                                         void* /*platform_instance*/)
{
    if (!window)
        throw std::runtime_error("Failed to create window surface: null window");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = glfwCreateWindowSurface(
        instance, static_cast<GLFWwindow*>(window), nullptr, &surface);
    if (r != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface (glfwCreateWindowSurface)");
    return surface;
}

void gfx_platform_destroy_surface(VkInstance instance, VkSurfaceKHR surface)
{
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

void gfx_platform_get_window_size(void* window, uint32_t* out_w, uint32_t* out_h)
{
    int w = 0, h = 0;
    if (window)
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(window), &w, &h);
    if (out_w)
        *out_w = w > 0 ? static_cast<uint32_t>(w) : 0;
    if (out_h)
        *out_h = h > 0 ? static_cast<uint32_t>(h) : 0;
}

void gfx_platform_imgui_init(void* window)
{
    // install_callbacks=true so GLFW keys/mouse feed ImGui.
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window), true);
}

void gfx_platform_imgui_shutdown()
{
    ImGui_ImplGlfw_Shutdown();
}

void gfx_platform_imgui_new_frame()
{
    ImGui_ImplGlfw_NewFrame();
}

void gfx_platform_debug_log(const char* msg)
{
    if (msg)
    {
        std::fputs(msg, stderr);
        std::fputc('\n', stderr);
    }
}

bool gfx_platform_ensure_directory(const char* path_utf8)
{
    if (!path_utf8 || !path_utf8[0])
        return false;
    std::error_code ec;
    std::filesystem::create_directories(path_utf8, ec);
    return !ec || std::filesystem::is_directory(path_utf8);
}

bool gfx_platform_save_window_png(void* window, const char* path_utf8)
{
    // No GPU readback wired yet: write a tiny placeholder PNG so F12 does not crash.
    // Full client capture can use swapchain readback in a later pass.
    (void)window;
    if (!path_utf8 || !path_utf8[0])
        return false;
    const int w = 4, h = 4;
    std::vector<unsigned char> rgba(static_cast<size_t>(w * h * 4), 32);
    for (int i = 0; i < w * h; ++i)
    {
        rgba[static_cast<size_t>(i) * 4 + 0] = 255; // brand-ish orange-ish placeholder
        rgba[static_cast<size_t>(i) * 4 + 1] = 92;
        rgba[static_cast<size_t>(i) * 4 + 2] = 0;
        rgba[static_cast<size_t>(i) * 4 + 3] = 255;
    }
    std::printf("[gfx] screenshot: Linux stub PNG (no window blit yet): %s\n", path_utf8);
    return write_png_rgba(path_utf8, w, h, rgba.data());
}
