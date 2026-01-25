#include <vulkan/vulkan.h>

#include <stdexcept>

  //instance.cpp
VkInstance create_instance();
void destroy_instance(VkInstance instance);

  //validation.cpp
void create_debug_messenger(VkInstance instance);
void destroy_debug_messenger(VkInstance instance);

