#include "gpu_prv_lib.h"


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
