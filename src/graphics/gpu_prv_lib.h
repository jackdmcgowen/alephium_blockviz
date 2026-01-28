#include <vulkan/vulkan.h>

#include <stdexcept>
#include <vector>

  //instance.cpp
VkInstance create_instance();
void destroy_instance(VkInstance instance);

  //validation.cpp
void create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance);

  //surface.cpp
VkSurfaceKHR create_win32_surface(VkInstance instance, void* hwnd, void* hinstance);
void destroy_surface(VkInstance instance, VkSurfaceKHR surface);

  //device.cpp
VkPhysicalDevice pick_physical_device(VkInstance instance);
void create_device(
    VkInstance          instance,
    VkPhysicalDevice    physicalDevice,
    VkDevice           *device,
    VkQueue            *queue);
void destroy_device(VkDevice device);

  //swapchain.cpp
void create_swapchain(
    VkDevice device,
    VkSurfaceKHR surface,
    VkSwapchainKHR* swapchain,
    std::vector<VkImage>& swapchainImages,
    VkFormat format,
    VkExtent2D extent);

