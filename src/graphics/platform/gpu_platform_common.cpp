#include "graphics/pch.h"
#include "graphics/platform/gpu_platform.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
bool g_headless = false;
uint32_t g_headless_w = 1280;
uint32_t g_headless_h = 720;

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
} // namespace

void gpu_platform_configure_headless(bool enabled, uint32_t width, uint32_t height)
{
    g_headless = enabled;
    if (width > 0)
        g_headless_w = width;
    if (height > 0)
        g_headless_h = height;
}

bool gpu_platform_is_headless()
{
    return g_headless;
}

uint32_t gpu_platform_headless_width()
{
    return g_headless_w;
}

uint32_t gpu_platform_headless_height()
{
    return g_headless_h;
}

VkSurfaceKHR gpu_platform_create_headless_surface(VkInstance instance)
{
    VkHeadlessSurfaceCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    auto fn = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
    if (!fn)
        throw std::runtime_error("vkCreateHeadlessSurfaceEXT not available "
                                 "(need VK_EXT_headless_surface)");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (fn(instance, &ci, nullptr, &surface) != VK_SUCCESS || surface == VK_NULL_HANDLE)
        throw std::runtime_error("Failed to create VK_EXT_headless_surface");
    return surface;
}

bool gpu_platform_write_png_rgba(const char* path_utf8, int w, int h, const unsigned char* rgba)
{
    if (!path_utf8 || w < 1 || h < 1 || !rgba)
        return false;

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4));
    for (int y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter None
        const unsigned char* row = rgba + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
        raw.insert(raw.end(), row, row + static_cast<size_t>(w) * 4);
    }

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
    uint32_t s1 = 1, s2 = 0;
    for (unsigned char b : raw)
    {
        s1 = (s1 + b) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    write_be32(z, (s2 << 16) | s1);

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
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    append_chunk(png, "IHDR", ihdr, 13);
    append_chunk(png, "IDAT", z.data(), z.size());
    append_chunk(png, "IEND", nullptr, 0);

    FILE* f = std::fopen(path_utf8, "wb");
    if (!f)
        return false;
    const size_t n = std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    return n == png.size();
}
