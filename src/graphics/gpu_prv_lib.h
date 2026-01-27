#include <vulkan/vulkan.h>

#include <stdexcept>

  //instance.cpp
VkInstance create_instance();
void destroy_instance(VkInstance instance);

  //validation.cpp
void create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance);

  //surface.cpp
VkSurfaceKHR create_win32_surface(VkInstance instance, void* hwnd, void* hinstance);
void destroy_surface(VkInstance instance, VkSurfaceKHR surface);

