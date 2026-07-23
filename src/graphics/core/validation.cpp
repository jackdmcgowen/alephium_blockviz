#include "graphics/pch.h"
#include "gpu_prv_lib.h"
#include "graphics/platform/gpu_platform.hpp"

#include <cstdio>

#ifndef NDEBUG
static const bool kCompileTimeValidation = true;
static const bool kCompileTimeValidationLogging = true;
#else
static const bool kCompileTimeValidation = false;
static const bool kCompileTimeValidationLogging = false;
#endif

static FILE* validationFile = nullptr;
static VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
static bool messenger_created = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback
(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    if (kCompileTimeValidationLogging && validationFile) {
        fprintf(validationFile, "%s\n", pCallbackData->pMessage);
        fflush(validationFile); // survive hard kill during /vulkan-validator re-verify
    }
    gpu_platform_debug_log(pCallbackData->pMessage);

    return VK_FALSE; // Return VK_FALSE to not abort the call

}   /* debug_callback() */


void create_debug_messenger(VkInstance instance)
{
    if (!kCompileTimeValidation)
        return;

    if (kCompileTimeValidationLogging) {
        // CWD is repo root (LocalDebuggerWorkingDirectory); keep logs out of source tree.
        gpu_platform_ensure_directory("build");
        validationFile = fopen("build/debug.log", "a+");

        if (!validationFile)
            throw std::runtime_error("Failed to setup debug log (build/debug.log)");
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
    messenger_created = true;

}   /* create_debug_messenger() */


void destroy_debug_messenger(VkInstance instance)
{
    if (validationFile)
    {
        fclose(validationFile);
        validationFile = nullptr;
    }
    if (messenger_created && debugMessenger != VK_NULL_HANDLE)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func)
            func(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
        messenger_created = false;
    }

}   /* destroy_debug_messenger() */
