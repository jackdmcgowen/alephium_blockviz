#include "graphics/pch.h"
#include "gpu_prv_lib.h"


void load_shader_source(const char* const   filename,
    std::vector<uint8_t>& src)
{
    char dirpath[128] = { 0 };

    snprintf(dirpath, 128, "src/graphics/shaders/%s", filename);

    FILE* file = fopen(dirpath, "rb");
    if (!file) {
        throw std::runtime_error("Failed to load shader");
    }

    fseek(file, 0, SEEK_END);
    long sz = ftell(file);
    fseek(file, 0, SEEK_SET);
    src.resize(sz);
    fread(src.data(), 1, sz, file);
    fclose(file);

}   /* load_shader_source() */


void create_shader_module(VkDevice device, VkShaderModule& shaderModule, std::vector<uint8_t>& pCode )
{
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = pCode.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(pCode.data());

    vkCreateShaderModule(device, &shaderInfo, nullptr, &shaderModule );

}	/* create_shader_module() */


void destroy_shader_module(VkDevice device, VkShaderModule shaderModule)
{
    vkDestroyShaderModule(device, shaderModule, nullptr);

}	/* destroy_shader_module() */
