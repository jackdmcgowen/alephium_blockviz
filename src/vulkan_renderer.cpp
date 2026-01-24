#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <cjson/cJSON.h>
#include "vulkan_renderer.hpp"
#include "commands.h"


#include "imgui.h"
#include "imgui_impl_win32.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include "imgui_impl_vulkan.h"
#include <windows.h>


#include <vulkan/vulkan_win32.h>
#include <vulkan/vk_enum_string_helper.h>


static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS)
        return;

    fprintf(stderr, "[vulkan] Error: VkResult = %s\n", string_VkResult(err) );
    if (err < 0)
        abort();
}

static const float FOV = glm::radians(45.0f);

#ifndef NDEBUG
static const bool enableValidationLayers = TRUE;
static const bool enableValidationLogging = TRUE;
#else
static const bool enableValidationLayers = FALSE;
static const bool enableValidationLogging = FALSE;
#endif
FILE             *validationFile;

const glm::vec3 SHARD_COLORS[16] = {
    glm::vec3(1.00f, 0.34f, 0.20f),  // #FF5733 Orange
    glm::vec3(0.20f, 1.00f, 0.34f),  // #33FF57 Green
    glm::vec3(0.20f, 0.34f, 1.00f),  // #3357FF Blue
    glm::vec3(1.00f, 0.20f, 1.00f),  // #FF33FF Pink
    glm::vec3(1.00f, 0.76f, 0.00f),  // #FFC300 Yellow
    glm::vec3(0.85f, 0.97f, 0.65f),  // #DAF7A6 Light Green
    glm::vec3(0.78f, 0.00f, 0.22f),  // #C70039 Dark Red
    glm::vec3(0.34f, 0.09f, 0.27f),  // #581845 Dark Purple
    glm::vec3(1.00f, 1.00f, 1.00f),  // #FFFFFF White
    glm::vec3(0.50f, 0.50f, 0.00f),  // #808000 Olive
    glm::vec3(0.00f, 1.00f, 1.00f),  // #00FFFF Aqua
    glm::vec3(1.00f, 0.75f, 0.80f),  // #FFC0CB Pink
    glm::vec3(0.50f, 0.00f, 0.50f),  // #800080 Purple
    glm::vec3(1.00f, 1.00f, 0.00f),  // #FFFF00 Yellow
    glm::vec3(0.50f, 0.50f, 0.50f),  // #808080 Grey
    glm::vec3(0.00f, 1.00f, 0.00f)   // #00FF00 Lime
};

const VulkanRenderer::Vertex VulkanRenderer::CUBE_VERTICES[8] = {
    { glm::vec3(-1, -1,  1), glm::normalize(glm::vec3(-1, -1,  1)) }, // 0
    { glm::vec3(1, -1,  1),  glm::normalize(glm::vec3(1, -1,  1)) }, // 1
    { glm::vec3(-1, -1, -1), glm::normalize(glm::vec3(-1, -1, -1)) }, // 2
    { glm::vec3(-1,  1, -1), glm::normalize(glm::vec3(-1,  1, -1)) }, // 3
    { glm::vec3(-1,  1,  1), glm::normalize(glm::vec3(-1,  1,  1)) }, // 4
    { glm::vec3(1,  1, -1),  glm::normalize(glm::vec3(1,  1, -1)) }, // 5
    { glm::vec3(1, -1, -1),  glm::normalize(glm::vec3(1, -1, -1)) }, // 6
    { glm::vec3(1,  1,  1),  glm::normalize(glm::vec3(1,  1,  1)) }  // 7
};

const uint16_t VulkanRenderer::CUBE_INDICES[36] = {
    6, 0, 2, 6, 1, 0,
    4, 0, 1, 0, 4, 3,
    0, 3, 2, 3, 5, 2,
    5, 6, 2, 6, 5, 7,
    6, 7, 1, 1, 7, 4,
    3, 4, 7, 7, 5, 3
};

VulkanRenderer::VulkanRenderer()
    : hInstance(nullptr)
    , hwnd(nullptr)
    , instance(VK_NULL_HANDLE)
    , physicalDevice(VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , graphicsQueue(VK_NULL_HANDLE)
    , surface(VK_NULL_HANDLE)
    , swapchain(VK_NULL_HANDLE)
    , renderPass(VK_NULL_HANDLE)
    , descriptorSetLayout(VK_NULL_HANDLE)
    , pipelineLayout(VK_NULL_HANDLE)
    , graphicsPipeline(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , inFlightFrames{}
    , currentFrame(0)
    , vertexBuffer(VK_NULL_HANDLE)
    , vertexBufferMemory(VK_NULL_HANDLE)
    , indexBuffer(VK_NULL_HANDLE)
    , indexBufferMemory(VK_NULL_HANDLE)
    , instanceBuffer(VK_NULL_HANDLE)
    , instanceBufferMemory(VK_NULL_HANDLE)
    , mappedInstanceMemory(nullptr)
    , instanceCount(0)
    , uniformBuffer(VK_NULL_HANDLE)
    , uniformBufferMemory(VK_NULL_HANDLE)
    , descriptorPool(VK_NULL_HANDLE)
    , descriptorSet(VK_NULL_HANDLE)
    , running(false)
    , timeOffset(0.0f)
{
}

VulkanRenderer::~VulkanRenderer()
{
    stop();
    cleanup();
}

void VulkanRenderer::init(void *hInstance, void *hwnd)
{
    RECT rect;
    GetClientRect((HWND)hwnd, &rect);
    width = static_cast<uint32_t>(rect.right - rect.left);
    height = static_cast<uint32_t>(rect.bottom - rect.top);

    this->hInstance = hInstance;
    this->hwnd = hwnd;
    create_instance();
    setup_debug_messenger();
    create_surface();
    pick_physical_device();
    create_logical_device();
    create_swapchain();
    create_depth_resources();
    create_image_views();
    create_render_pass();
    create_descriptor_set_layout();
    create_graphics_pipeline();
    create_framebuffers();
    create_command_pool();
    create_vertex_buffer();
    create_index_buffer();
    create_instance_buffer();
    create_uniform_buffer();
    create_descriptor_pool();
    create_descriptor_sets();
    create_command_buffers();
    create_sync_objects();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    ImGuiStyle& style = ImGui::GetStyle();


    // Set background color to dark grey
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);

    // Set text color to light grey
    style.Colors[ImGuiCol_Text] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    // Set button color
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f); // Dark button color
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f); // Slightly lighter on hover

    

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplVulkan_InitInfo info = {};

    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance;
    info.PhysicalDevice = physicalDevice;
    info.Device = device;
    info.QueueFamily = 0; // Assume graphics queue at index 0
    info.Queue = graphicsQueue;
    info.DescriptorPool = descriptorPool;
    info.PipelineCache = VK_NULL_HANDLE;
    info.RenderPass = renderPass;
    info.UseDynamicRendering = false;
    info.Subpass = 0;
    info.MinImageCount = 2;
    info.ImageCount = 2;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.Allocator = NULL;
    info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init( &info );
    
}

void VulkanRenderer::add_block(cJSON* block)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    blockSet.insert(AlphBlock(block));
    dataCond.notify_one();
}

void VulkanRenderer::start()
{
    running = true;
    renderThread = std::thread(&VulkanRenderer::render_loop, this);
}

void VulkanRenderer::stop()
{
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        running = false;
    }
    dataCond.notify_one();
    if (renderThread.joinable())
    {
        renderThread.join();
    }
}

void VulkanRenderer::render_loop()
{
    const double frameTimeMin = 1000.0 / 60; // ~16.67ms for 60Hz
    LARGE_INTEGER freq, t1, t2;
    double t, dt;

    t = dt = 0.0;
    QueryPerformanceFrequency(&freq);

    while (running)
    {
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        //start frame
        QueryPerformanceCounter(&t1);

        //delay 16 seconds
        int64_t delaytime = static_cast<int64_t>(time(NULL) - 16) * 1000;

        std::unique_lock<std::mutex> lock(dataMutex);
        if (!blockSet.empty())
        {
            auto it = blockSet.begin();
            AlphBlock block = *it;

            if (delaytime > block.timestamp)
            {
                int shardId = block.chainFrom * 4 + block.chainTo;
                float angle = (shardId / 16.0f) * 2.0f * glm::pi<float>();
                float radius = 20.0f;
                float x = radius * cosf(angle);
                float y = radius * sinf(angle);
                float z = static_cast<float>(block.timestamp) * 0.00000000001f - timeOffset;

                InstanceData inst = { glm::vec3(x, y, z), SHARD_COLORS[shardId] };

                if (instanceCount < MAX_INSTANCES)
                {
                    memcpy(static_cast<char*>(mappedInstanceMemory) + instanceCount * sizeof(InstanceData), &inst, sizeof(InstanceData));
                    instanceCount++;
                }
                else
                {
                    printf("Instance buffer full\n");
                }

                blockQueue.push_front(block);
                blockSet.erase(it);
            }
        }

        lock.unlock();

        if( blockQueue.size() > 50)
        {
            blockQueue.pop_back();
        }

        ImGui::Begin("Blockflow");
        for (auto it2 : blockQueue)
        {
            ImGui::PushID(it2.hash.c_str());

            int shardId = it2.chainFrom * 4 + it2.chainTo;
            ImGui::TextColored( ImVec4(SHARD_COLORS[shardId].r, SHARD_COLORS[shardId].g, SHARD_COLORS[shardId].b, 1.0f), "[%d->%d]", it2.chainFrom, it2.chainTo );
            ImGui::SameLine();
           
      
            if (ImGui::Button(it2.hash.c_str()))
            {
                ImGui::SetClipboardText(it2.hash.c_str());
            }
            ImGui::PopID();
        }
        ImGui::End();

        ImGui::Render();
        update_uniform_buffer();
        render();

        timeOffset += static_cast<float>(dt) * 0.001f; // Scroll speed

        //end frame
        do
        {
            QueryPerformanceCounter(&t2);
            dt = static_cast<double>((t2.QuadPart - t1.QuadPart) * 1000LL / freq.QuadPart);
            t += dt;
        } while (dt <= frameTimeMin);


        
    }
}

void VulkanRenderer::render()
{
    static uint64_t s_frameCounter;
    static uint32_t imageIndex;

    VkFence         fence;
    VkCommandBuffer commandBuffer;
    VkSemaphore     imageAvailableSemaphore;
    VkSemaphore     renderFinishedSemaphore;

    std::lock_guard<std::mutex> lk(renderMutex);

    currentFrame = s_frameCounter % MAX_FRAMES_IN_FLIGHT;

    fence = inFlightFrames[currentFrame].fence;
    commandBuffer = inFlightFrames[currentFrame].commandBuffer;
    imageAvailableSemaphore = inFlightFrames[currentFrame].imageAvailableSemaphore;
    renderFinishedSemaphore = inFlightFrames[currentFrame].renderFinishedSemaphore;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence );

    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    
    vkResetCommandBuffer(commandBuffer, 0);
    record_command_buffer(commandBuffer, imageIndex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST );

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(graphicsQueue, &presentInfo);

    s_frameCounter++;
}

void VulkanRenderer::create_instance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Alephium DAG";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    const char* extensions[] = 
    { 
#ifndef NDEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
    const char* enbl_layers[] = { "VK_LAYER_KHRONOS_validation" };
#ifndef NDEBUG
    createInfo.enabledExtensionCount = 3;
#else
    createInfo.enabledExtensionCount = 2;
#endif
    createInfo.ppEnabledExtensionNames = extensions;
#ifndef NDEBUG
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = enbl_layers;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void VulkanRenderer::create_surface()
{
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)hwnd;
    createInfo.hinstance = (HINSTANCE)hInstance;

    if (vkCreateWin32SurfaceKHR( instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface");
    }
}

void VulkanRenderer::pick_physical_device()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        throw std::runtime_error("No Vulkan-capable devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::vector<VkPhysicalDeviceProperties> deviceProps(deviceCount);

    physicalDevice = devices[0]; // Pick first device (simplified)
    for (uint32_t i = 0; i < deviceCount; ++i)
    {
        vkGetPhysicalDeviceProperties(devices[i], &deviceProps[i] );
        if (deviceProps[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) //Pick discrete GPU (specific)
        {
            physicalDevice = devices[i];
            printf("%s\n", deviceProps[i].deviceName);
            break;
        }
    }

    


}

void VulkanRenderer::create_logical_device()
{
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0; // Assume graphics queue at index 0
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);
}

void VulkanRenderer::resize()
{
    RECT rect;
    int new_width;
    int new_height;

    GetClientRect((HWND)hwnd, &rect);

    new_width = rect.right - rect.left;
    new_height = rect.bottom - rect.top;

    if (new_width == width && new_height == height)
        return;

    std::lock_guard<std::mutex> lk(renderMutex);

    width = new_width;
    height = new_height;

    vkDeviceWaitIdle(device);
    
    for (auto framebuffer : swapchainFramebuffers)
    {
        vkDestroyFramebuffer(device, framebuffer, nullptr);

    }


    for (auto imageView : swapchainImageViews)
    {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);

    create_swapchain();
    create_depth_resources();
    create_image_views();
    create_framebuffers();
}

void VulkanRenderer::create_swapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = 3;
    createInfo.oldSwapchain = swapchain;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = { width, height };
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain");
    }

    uint32_t imageCount;
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
    swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    swapchainExtent = { width, height };
}

void VulkanRenderer::create_depth_resources()
{
    VkFormat depthFormat = find_depth_format();
    create_image(width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
    depthImageView = create_image_view(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanRenderer::create_image_views()
{
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++)
    {
        swapchainImageViews[i] = create_image_view(swapchainImages[i], swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanRenderer::create_render_pass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = find_depth_format();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkAttachmentDescription  attachments[2] = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass");
    }
}

void VulkanRenderer::create_descriptor_set_layout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

void VulkanRenderer::create_graphics_pipeline()
{
    // Simplified shader loading (assume SPIR-V shaders: vert.spv, frag.spv)
    FILE* vertFile = fopen("src/vert.spv", "rb");
    FILE* fragFile = fopen("src/frag.spv", "rb");
    if (!vertFile || !fragFile)
    {
        throw std::runtime_error("Failed to load shaders");
    }

    fseek(vertFile, 0, SEEK_END);
    long vertSize = ftell(vertFile);
    fseek(vertFile, 0, SEEK_SET);
    std::vector<char> vertShaderCode(vertSize);
    fread(vertShaderCode.data(), 1, vertSize, vertFile);
    fclose(vertFile);

    fseek(fragFile, 0, SEEK_END);
    long fragSize = ftell(fragFile);
    fseek(fragFile, 0, SEEK_SET);
    std::vector<char> fragShaderCode(fragSize);
    fread(fragShaderCode.data(), 1, fragSize, fragFile);
    fclose(fragFile);

    VkShaderModuleCreateInfo vertShaderInfo{};
    vertShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertShaderInfo.codeSize = vertShaderCode.size();
    vertShaderInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    VkShaderModule vertShaderModule;
    vkCreateShaderModule(device, &vertShaderInfo, nullptr, &vertShaderModule);

    VkShaderModuleCreateInfo fragShaderInfo{};
    fragShaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragShaderInfo.codeSize = fragShaderCode.size();
    fragShaderInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    VkShaderModule fragShaderModule;
    vkCreateShaderModule(device, &fragShaderInfo, nullptr, &fragShaderModule);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VkVertexInputBindingDescription bindingDescriptions[2];
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions[1].binding = 1;
    bindingDescriptions[1].stride = sizeof(InstanceData);
    bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos.x);
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal.x);

    VkVertexInputAttributeDescription instanceAttributes[2];
    instanceAttributes[0].binding = 1;
    instanceAttributes[0].location = 2;
    instanceAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[0].offset = offsetof(InstanceData, pos);
    instanceAttributes[1].binding = 1;
    instanceAttributes[1].location = 3;
    instanceAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    instanceAttributes[1].offset = offsetof(InstanceData, color);

    VkVertexInputAttributeDescription attributes[] = { attributeDescriptions[0], attributeDescriptions[1], instanceAttributes[0], instanceAttributes[1] };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 2;
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_VIEWPORT
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = _countof( dynamicStates );
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void VulkanRenderer::create_framebuffers()
{
    swapchainFramebuffers.clear();
    swapchainFramebuffers.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        VkImageView attachments[] = { swapchainImageViews[i], depthImageView };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void VulkanRenderer::create_vertex_buffer()
{
    VkDeviceSize bufferSize = sizeof(CUBE_VERTICES);
    create_buffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertexBuffer, vertexBufferMemory);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, CUBE_VERTICES, bufferSize);
    vkUnmapMemory(device, vertexBufferMemory);
}

void VulkanRenderer::create_index_buffer()
{
    VkDeviceSize bufferSize = sizeof(CUBE_INDICES);
    create_buffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexBuffer, indexBufferMemory);

    void* data;
    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, CUBE_INDICES, bufferSize);
    vkUnmapMemory(device, indexBufferMemory);
}

void VulkanRenderer::create_instance_buffer()
{
    VkDeviceSize bufferSize = sizeof(InstanceData) * MAX_INSTANCES;
    create_buffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceBuffer, instanceBufferMemory);

    vkMapMemory(device, instanceBufferMemory, 0, bufferSize, 0, &mappedInstanceMemory);
    instanceCount = 0;
}

void VulkanRenderer::create_uniform_buffer()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    create_buffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer, uniformBufferMemory);
}


void VulkanRenderer::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = 
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = pool_sizes;
    poolInfo.maxSets = 2;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

void VulkanRenderer::create_descriptor_sets()
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor sets");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanRenderer::create_command_pool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assume graphics family at 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkAllocateCommandBuffers(device, &allocInfo, &inFlightFrames[i].commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers");
        }
    }
}

void VulkanRenderer::create_command_buffers()
{
    // Empty - handled in render_loop()
}


void VulkanRenderer::create_sync_objects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &inFlightFrames[i].imageAvailableSemaphore) != VK_SUCCESS
         || vkCreateSemaphore(device, &semaphoreInfo, nullptr, &inFlightFrames[i].renderFinishedSemaphore) != VK_SUCCESS
         || vkCreateFence(device, &fenceInfo, nullptr, &inFlightFrames[i].fence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects");
        }
    }
}


void VulkanRenderer::update_uniform_buffer()
{
    UniformBufferObject ubo{};

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, -30.0f - timeOffset);
    glm::vec3 center = glm::vec3(0.0f, 0.0f, -timeOffset);

    std::lock_guard<std::mutex> lk(renderMutex);

    ubo.view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective( FOV, (float)width / height, NEAR_PLANE, FAR_PLANE );
    //ubo.proj = glm::frustum(-NEAR_PLANE, (float)NEAR_PLANE, -(float)NEAR_PLANE, (float)NEAR_PLANE, NEAR_PLANE, FAR_PLANE);
    //ubo.proj[1][1] *= -1; // Flip Y for Vulkan

    ubo.viewPos = eye;
    ubo.lightPos = center;

    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniformBufferMemory);
}

void VulkanRenderer::record_command_buffer(VkCommandBuffer buffer, uint32_t imageIndex, VkPrimitiveTopology topology)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(buffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapchainExtent;
    VkClearValue clearValues[2];
    clearValues[0].color = { {0.7f, 0.7f, 0.7f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(buffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdSetPrimitiveTopology(buffer, topology);

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(buffer, 0, 1, &viewport);

    VkBuffer buffers[] = { vertexBuffer, instanceBuffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(buffer, 0, 2, buffers, offsets);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindIndexBuffer(buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(buffer, 36, static_cast<uint32_t>(instanceCount), 0, 0, 0); // 36 indices per cube
    
    ImDrawData* data = ImGui::GetDrawData();
    if (data)
    {
        ImGui_ImplVulkan_RenderDrawData(data, buffer);
    }
    vkCmdEndRenderPass(buffer);

    if (vkEndCommandBuffer(buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer");
    }

    
}

uint32_t VulkanRenderer::find_memory_type(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

void VulkanRenderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, memory, 0);
}

void VulkanRenderer::create_image(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = find_memory_type(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate depth image memory");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

VkImageView VulkanRenderer::create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image view");
    }
    return imageView;
}

VkFormat VulkanRenderer::find_depth_format()
{
    VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }
    throw std::runtime_error("Failed to find supported depth format");
}

void VulkanRenderer::cleanup()
{
    vkDeviceWaitIdle(device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (mappedInstanceMemory)
    {
        vkUnmapMemory(device, instanceBufferMemory);
    }
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthImageMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, instanceBuffer, nullptr);
    vkFreeMemory(device, instanceBufferMemory, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(device, inFlightFrames[i].imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(device, inFlightFrames[i].renderFinishedSemaphore, nullptr);
        vkDestroyFence(device, inFlightFrames[i].fence, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
    for (auto framebuffer : swapchainFramebuffers)
    {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (auto imageView : swapchainImageViews)
    {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);

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
    vkDestroyInstance(instance, nullptr);
}

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
    OutputDebugStringA( pCallbackData->pMessage );

    return VK_FALSE; // Return VK_FALSE to not abort the call
}

void VulkanRenderer::setup_debug_messenger()
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

