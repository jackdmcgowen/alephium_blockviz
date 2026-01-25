#include "gpu_prv_lib.h"

#include <windows.h> //OutputDebugString

#ifndef NDEBUG
static const bool enableValidationLayers = TRUE;
static const bool enableValidationLogging = TRUE;
#else
static const bool enableValidationLayers = FALSE;
static const bool enableValidationLogging = FALSE;
#endif

FILE* validationFile;
VkDebugUtilsMessengerEXT debugMessenger;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback
(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    if (enableValidationLogging) {
        fprintf(validationFile, "%s", pCallbackData->pMessage);
    }
    OutputDebugStringA(pCallbackData->pMessage);

    return VK_FALSE; // Return VK_FALSE to not abort the call

}   /* debug_callback() */


void create_debug_messenger(VkInstance instance)
{
    if (!enableValidationLayers) return;

    if (enableValidationLogging) {
        validationFile = fopen( "debug.log", "a+");

        if (!validationFile)
            throw std::runtime_error("Failed to setup debug log");
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debug_callback;
    createInfo.pUserData = nullptr; // Optional

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func == nullptr || func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}


void destroy_debug_messenger(VkInstance instance)
{
    if (enableValidationLayers)
    {
        if (enableValidationLogging)
        {
            fclose(validationFile);
            validationFile = NULL;

            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            func(instance, debugMessenger, nullptr);
        }
    }

}   /* destroy_debug_messenger() */