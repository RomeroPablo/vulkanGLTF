#include "SDL_keyboard.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/detail/qualifier.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <numeric>
#include <ratio>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define SDL_MAIN_HANDLED 1
#endif

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_vulkan.h>
#else
#include <SDL.h>
#include <SDL_main.h>
#include <SDL_vulkan.h>
#endif

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include "lib.hpp"
#include "gltf.hpp"

static std::filesystem::file_time_type getFileTimestamp(const char* path) {
    std::error_code ec;
    std::filesystem::file_time_type ts = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }
    return ts;
}

struct alignas(16) MVP {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 camPos;
};

struct PushConstants{
    float resolution[2];
    float time;
    float _pad;
};

struct alignas(16) MaterialParams {
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float normalScale{1.0f};
    float occlusionStrength{1.0f};
    float alphaCutoff{0.5f};
    int alphaMode{0}; // 0: OPAQUE, 1: MASK, 2: BLEND
    int hasBaseColorTexture{0};
    int hasMetallicRoughnessTexture{0};
    int hasNormalTexture{0};
    int hasOcclusionTexture{0};
    int hasEmissiveTexture{0};
};

struct State{
    const char* shaderFragPath = "kernels/shader.frag";
    const char* shaderVertPath = "kernels/shader.vert";
    const char* gltfFragPath = "kernels/gltf.frag";
    const char* gltfVertPath = "kernels/gltf.vert";
    const char* postFragPath = "kernels/postProcess.frag";
    const char* postVertPath = "kernels/postProcess.vert";
    std::filesystem::file_time_type fragTs{};
    SDL_Window* window;
    uint32_t width = 1280;
    uint32_t height = 720;
    bool running = true;
    std::chrono::time_point<std::chrono::steady_clock> tStart{}, tEnd{};
    std::chrono::duration<float> runtime{};
    std::chrono::duration<float> frameTime{1.0f / 300.0f};
    float dt;

    std::vector<const char*> sdlExtensions{};
    std::vector<const char*> layers{};
    std::vector<const char*> extensions{};
    std::vector<const char*> deviceExtensions{};

    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    VkDevice logicalDevice;

    uint32_t familyIndex;
    uint32_t queueIndex;
    uint32_t queueCount;
    float    priority;
    VkQueue  queue;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    VkFormat swapchainImageFormat;
    VkColorSpaceKHR swapchainColorspace;
    VkSurfaceTransformFlagBitsKHR swapchainTransform;
    VkCompositeAlphaFlagBitsKHR swapchainAlpha;
    VkPresentModeKHR swapchainPresentMode;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthImageMemory;
    std::vector<VkImageView> depthImageViews;

    VkRenderPass renderPass;
    VkRenderPass postRenderPass{VK_NULL_HANDLE};
    std::vector<VkAttachmentDescription> attachmentDescriptions;
    std::vector<VkSubpassDescription> subpassDescriptions;
    std::vector<VkSubpassDependency> subpassDependencies;

    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkFramebuffer> postFramebuffers;

    std::vector<VkImage> offscreenColorImages;
    std::vector<VkDeviceMemory> offscreenColorImageMemory;
    std::vector<VkImageView> offscreenColorImageViews;
    VkSampler offscreenColorSampler{VK_NULL_HANDLE};

    VkPipelineLayout shaderPipelineLayout{VK_NULL_HANDLE};
    VkPipeline shaderPipeline{VK_NULL_HANDLE};
    VkPipelineLayout gltfPipelineLayout{VK_NULL_HANDLE};
    VkPipeline gltfPipeline{VK_NULL_HANDLE};
    VkPipelineLayout postPipelineLayout{VK_NULL_HANDLE};
    VkPipeline postPipeline{VK_NULL_HANDLE};

    VkViewport viewport;
    VkRect2D scissor;

    VkShaderModule fragModule;
    VkShaderModule vertModule;
    VkShaderModule gltfFragModule{VK_NULL_HANDLE};
    VkShaderModule gltfVertModule{VK_NULL_HANDLE};
    VkShaderModule postFragModule{VK_NULL_HANDLE};
    VkShaderModule postVertModule{VK_NULL_HANDLE};

    uint32_t frameIndex{0uz};
    std::vector<VkFence> fences;
    std::vector<VkSemaphore> renderCompleteSemaphores;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<Vertex> vertices;
    std::vector<VkVertexInputAttributeDescription> vAttributeDescriptions;
    VkVertexInputBindingDescription vBindingDescription;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout uniformDescriptorSetLayout;
    std::vector<VkDescriptorSetLayout> uboLayouts;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBufferMemory;
    std::vector<void*> uniformBufferMapped;

    MVP mvp = {};

    struct {
       glm::vec3 position = {};
       glm::vec3 front = {};
       glm::vec3 up = {};
       float yaw = 0.0;
       float pitch = 0.0;
    } camera;

    struct {
        bool in{};
        bool out{};
        bool left{};
        bool right{};
        bool up{};
        bool down{};
    } move;

    bool enable_input = true;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    bool gltfLoaded = false;

    struct TextureResource {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
    };

    struct MaterialRuntime {
        MaterialParams params{};
        int baseColorTextureIndex{-1};
        int metallicRoughnessTextureIndex{-1};
        int normalTextureIndex{-1};
        int occlusionTextureIndex{-1};
        int emissiveTextureIndex{-1};
    };

    struct DrawItem {
        uint32_t firstVertex{0};
        uint32_t vertexCount{0};
        uint32_t materialIndex{0};
    };

    std::vector<TextureResource> gltfTexturesSrgb;
    std::vector<TextureResource> gltfTexturesLinear;
    TextureResource fallbackWhiteTexture{};
    std::vector<MaterialRuntime> materials;
    std::vector<DrawItem> drawItems;

    VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> materialDescriptorSets;
    std::vector<VkBuffer> materialUniformBuffers;
    std::vector<VkDeviceMemory> materialUniformBufferMemory;
    VkDescriptorSetLayout postDescriptorSetLayout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> postDescriptorSets;

    void initVulkan();
    void setExtensions();
    void setLayers();
    void initInstance();

    uint32_t findMemoryType(VkMemoryPropertyFlags f, uint32_t typeFilter);

    void initDevice();
    void initFramebuffer();
    void initGltf();
    void initShaders();
    void initUniforms();
    void buildPipeline(VkShaderModule fragModule);
    void initResources();
    void runRenderPass(uint32_t imgIdx, VkPipeline pipeline);
    void rebuildFragShader();
    void updateUniforms();
    void loadGltfTextures();
    void createFallbackTexture();
    void createMaterialResources();
    TextureResource createTexture2DFromRGBA(const unsigned char* rgba, uint32_t width, uint32_t height, VkFormat format);
    void destroyTexture(TextureResource& texture);
    void buildGltfPipeline();
    void buildPostPipeline();
    void runRenderPassGltf(uint32_t imgIdx);

    void initSDL();
    void getInput();
    void renderLoop();
    void appLogic();
    void exit();
};

void State::setExtensions(){
    uint32_t count;
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    VkExtensionProperties availableExtensions[count];
    vkEnumerateInstanceExtensionProperties(NULL, &count, availableExtensions);
    std::cout << "[+] Available Extensions" << std::endl;
    for(const auto& e : availableExtensions){
        std::cout << '\t' << e.extensionName << std::endl;
        for(const auto& s : sdlExtensions){
            if(strcmp(e.extensionName, s) == 0){
                extensions.push_back(s);
            }
        }
    }

    std::cout << "[+] Enabled Extensions:" << std::endl;
    for(const auto& s : extensions){
        std::cout << "\t" << s << std::endl;
    }
}

void State::setLayers(){
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    VkLayerProperties availableLayers[count];
    vkEnumerateInstanceLayerProperties(&count, availableLayers);
    std::cout << "[+] Available Layers" << std::endl;
    for(const auto& l : availableLayers){
        std::cout << '\t' << l.layerName << std::endl;
    }

    std::cout << "[+] Enabled Layers:" << std::endl;
    for(const auto& s : layers){
        std::cout << '\t' << s << std::endl;
    }
}

void State::initInstance(){
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "shaderEditor",
        .applicationVersion = VK_MAKE_VERSION(1,0,0),
        .pEngineName = "NULL",
        .engineVersion = VK_MAKE_VERSION(1,0,0),
        .apiVersion = VK_API_VERSION_1_0
    };
    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vkCreateInstance(&createInfo, NULL, &instance);
};

void State::initDevice(){
    uint32_t count;
    vkEnumeratePhysicalDevices(instance, &count, NULL);
    VkPhysicalDevice devices[count];
    vkEnumeratePhysicalDevices(instance, &count, devices);
    std::cout << "[+] Available Devices" << std::endl;
    for(const auto& d : devices){
        VkPhysicalDeviceProperties props;
        VkPhysicalDeviceFeatures feats;
        vkGetPhysicalDeviceProperties(d, &props);
        vkGetPhysicalDeviceFeatures(d, &feats);
        vkGetPhysicalDeviceMemoryProperties(d, &memoryProperties);
        printDeviceProps(props);
        std::cout << "\t\t"; printBreak(37);
        printSparseProps(props.sparseProperties);
        std::cout << "\t\t"; printBreak(37);
        printPhysicalLimits(props.limits);
        std::cout << "\t\t"; printBreak(37);
        printPhysicalFeatures(feats);
        std::cout << "\t\t"; printBreak(37);
        printMemoryProps(memoryProperties);
        std::cout << "\t\t"; printBreak(37);
    }

    physicalDevice = devices[0];

    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, NULL);
    VkQueueFamilyProperties queueProps[queueFamilyCount];
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueProps);
    for(const auto& q : queueProps){
        printQueueFamilyProperties(q);
        std::cout << "\t\t"; printBreak(37);
    }

    familyIndex = 0;
    queueCount = 1;
    queueIndex = 0;
    priority = 1.0f;

    VkDeviceQueueCreateInfo queueCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = familyIndex,
        .queueCount = queueCount,
        .pQueuePriorities = &priority
    };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos = {queueCI};

    VkPhysicalDeviceFeatures deviceFeatures = {
        .sampleRateShading = VK_TRUE,
        .fillModeNonSolid = VK_TRUE,
        .samplerAnisotropy = VK_TRUE,
    };

    uint32_t extCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extCount, NULL);
    VkExtensionProperties extensions[extCount];
    vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extCount, extensions);
    std::cout << "[+] Found Physical Device Extensions" << std::endl;
    for(const auto& e : extensions){
        std::cout << "\t" << e.extensionName << std::endl;
    };
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    VkDeviceCreateInfo deviceCI = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .enabledLayerCount = 0,         //legacy
        .ppEnabledLayerNames = NULL,    //legacy
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = NULL
    };
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCI, NULL, &logicalDevice));
    std::cout << "[+] Enabled Device Extensions:" << std::endl;
    for(const auto& s : deviceExtensions){
        std::cout << "\t" << s << std::endl;
    };
    vkGetDeviceQueue(logicalDevice, familyIndex, queueIndex, &queue);

    VkBool32 support = 0;
    assert(SDL_Vulkan_CreateSurface(window, instance, &surface));
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, surface, &support);
    assert(support);
}

void State::initFramebuffer(){
    uint32_t surfaceFormatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, NULL);
    VkSurfaceFormatKHR surfaceFormats[surfaceFormatCount];
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats);
    std::cout << "[+] Found " << surfaceFormatCount << " Formats" << std::endl;
    bool foundFormat = false;
    for(const auto& f : surfaceFormats){
        if(f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            std::cout << "[!] Found Requested Format" << std::endl;
            swapchainColorspace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
            foundFormat = true;
        }
    }; assert(foundFormat);

    uint32_t presentCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, NULL);
    VkPresentModeKHR presentModes[presentCount];
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentCount, presentModes);
    bool foundPresent = false;
    for(const auto& p : presentModes){
        printPresentMode(p);
        if(p == VK_PRESENT_MODE_IMMEDIATE_KHR){
            swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            foundPresent = true;
        }
    }; assert(foundPresent);

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    swapchainImages.resize(capabilities.minImageCount);

    swapchainTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkSwapchainCreateInfoKHR scCI = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = static_cast<uint32_t>(swapchainImages.size()),
        .imageFormat = swapchainImageFormat,
        .imageColorSpace = swapchainColorspace,
        .imageExtent = {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = swapchainTransform,
        .compositeAlpha = swapchainAlpha,
        .presentMode = swapchainPresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = NULL,
    };
    VK_CHECK(vkCreateSwapchainKHR(logicalDevice, &scCI, NULL, &swapchain));
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, NULL);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, swapchainImages.data());
    std::cout << "[+] Using " << swapchainImages.size() << " Swapchain Images" << std::endl;

    swapchainImageViews.resize(swapchainImages.size());
    for(auto i{0uz} ; i < swapchainImageViews.size(); i++){
        VkImageViewCreateInfo iCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainImageFormat,
            .components = {},
            .subresourceRange = { 
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, 
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        VK_CHECK(vkCreateImageView(logicalDevice, &iCI, NULL, &swapchainImageViews[i]));
    }

    auto findSupportedDepthFormat = [&]() -> VkFormat {
        const std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };
        for (VkFormat fmt : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                return fmt;
            }
        }
        return VK_FORMAT_D32_SFLOAT;
    };
    depthFormat = findSupportedDepthFormat();
    depthImages.resize(swapchainImages.size());
    depthImageMemory.resize(swapchainImages.size());
    depthImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageCreateInfo depthImageCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = depthFormat,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VK_CHECK(vkCreateImage(logicalDevice, &depthImageCI, nullptr, &depthImages[i]));

        VkMemoryRequirements depthReqs{};
        vkGetImageMemoryRequirements(logicalDevice, depthImages[i], &depthReqs);
        VkMemoryAllocateInfo depthAlloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = depthReqs.size,
            .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthReqs.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &depthAlloc, nullptr, &depthImageMemory[i]));
        VK_CHECK(vkBindImageMemory(logicalDevice, depthImages[i], depthImageMemory[i], 0));

        VkImageViewCreateInfo depthViewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        VK_CHECK(vkCreateImageView(logicalDevice, &depthViewCI, nullptr, &depthImageViews[i]));
    }

    renderCompleteSemaphores.resize(swapchainImages.size());
    VkSemaphoreCreateInfo sCI = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for(auto& s : renderCompleteSemaphores)
        VK_CHECK(vkCreateSemaphore(logicalDevice, &sCI, NULL, &s));

    imageAvailableSemaphores.resize(swapchainImages.size());
    for(auto& s : imageAvailableSemaphores)
        VK_CHECK(vkCreateSemaphore(logicalDevice, &sCI, NULL, &s));

    VkFenceCreateInfo fCI = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    fences.resize(swapchainImages.size());
    for(auto& f : fences)
        VK_CHECK(vkCreateFence(logicalDevice, &fCI, NULL, &f));

    offscreenColorImages.resize(swapchainImages.size());
    offscreenColorImageMemory.resize(swapchainImages.size());
    offscreenColorImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageCreateInfo imageCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = swapchainImageFormat,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        VK_CHECK(vkCreateImage(logicalDevice, &imageCI, nullptr, &offscreenColorImages[i]));

        VkMemoryRequirements memReqs{};
        vkGetImageMemoryRequirements(logicalDevice, offscreenColorImages[i], &memReqs);
        VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memReqs.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &alloc, nullptr, &offscreenColorImageMemory[i]));
        VK_CHECK(vkBindImageMemory(logicalDevice, offscreenColorImages[i], offscreenColorImageMemory[i], 0));

        VkImageViewCreateInfo viewCI = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = offscreenColorImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainImageFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        VK_CHECK(vkCreateImageView(logicalDevice, &viewCI, nullptr, &offscreenColorImageViews[i]));
    }

    VkSamplerCreateInfo samplerCI = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxAnisotropy = 1.0f,
    };
    VK_CHECK(vkCreateSampler(logicalDevice, &samplerCI, nullptr, &offscreenColorSampler));

    attachmentDescriptions.clear();
    subpassDescriptions.clear();
    subpassDependencies.clear();

    VkAttachmentDescription colorAttachment = {
        .flags = 0,
        .format = swapchainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    attachmentDescriptions.push_back(colorAttachment);

    VkAttachmentDescription depthAttachment = {
        .flags = 0,
        .format = depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    attachmentDescriptions.push_back(depthAttachment);

    VkAttachmentReference colorAttachmentReference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkAttachmentReference depthAttachmentReference = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription subpassDescription = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentReference,
        .pDepthStencilAttachment = &depthAttachmentReference,
    };
    subpassDescriptions.push_back(subpassDescription);

    VkSubpassDependency subpassDependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };
    subpassDependencies.push_back(subpassDependency);

    VkRenderPassCreateInfo rpCI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size()),
        .pAttachments = attachmentDescriptions.data(),
        .subpassCount = static_cast<uint32_t>(subpassDescriptions.size()),
        .pSubpasses = subpassDescriptions.data(),
        .dependencyCount = static_cast<uint32_t>(subpassDependencies.size()),
        .pDependencies = subpassDependencies.data()
    };
    VK_CHECK(vkCreateRenderPass(logicalDevice, &rpCI, nullptr, &renderPass));

    VkAttachmentDescription postColorAttachment = {
        .flags = 0,
        .format = swapchainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference postColorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription postSubpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &postColorRef,
    };
    VkSubpassDependency postDep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo postRpCI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &postColorAttachment,
        .subpassCount = 1,
        .pSubpasses = &postSubpass,
        .dependencyCount = 1,
        .pDependencies = &postDep,
    };
    VK_CHECK(vkCreateRenderPass(logicalDevice, &postRpCI, nullptr, &postRenderPass));

    framebuffers.resize(swapchainImages.size());
    postFramebuffers.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        std::array<VkImageView, 2> sceneViews = {
            offscreenColorImageViews[i],
            depthImageViews[i]
        };
        VkFramebufferCreateInfo sceneFbCI = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = static_cast<uint32_t>(sceneViews.size()),
            .pAttachments = sceneViews.data(),
            .width = width,
            .height = height,
            .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(logicalDevice, &sceneFbCI, nullptr, &framebuffers[i]));

        VkFramebufferCreateInfo postFbCI = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = postRenderPass,
            .attachmentCount = 1,
            .pAttachments = &swapchainImageViews[i],
            .width = width,
            .height = height,
            .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(logicalDevice, &postFbCI, nullptr, &postFramebuffers[i]));
    }
};

uint32_t State::findMemoryType(VkMemoryPropertyFlags f, uint32_t typeFilter){
    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++){
        if((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & f) == f)
            return i;
    }
    return UINT32_MAX;
};

#define MODEL_PATH "external/daybreak.glb"
#define HELMET_PATH "external/DamagedHelmet.gltf"
void State::initGltf(){
    std::cout << "[+] Init GLTF" << std::endl;
    std::string err = {}, warn = {};
    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, MODEL_PATH);
    //bool ok = loader.LoadASCIIFromFile(&model, &err, &warn, HELMET_PATH);
    if(!warn.empty()) std::cout << warn << std::endl;
    if(!err.empty()) std::cout << err << std::endl;
    if (!ok) {
        std::cerr << "[!] Failed to load glTF. Falling back to procedural geometry.\n";
        return;
    }

    int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size())) {
        std::cerr << "[!] glTF has no valid scene. Falling back to procedural geometry.\n";
        return;
    }

    vertices.clear();
    drawItems.clear();
    materials.clear();
    const tinygltf::Scene& scene = model.scenes[sceneIndex];
    std::vector<GltfModel::PrimitiveRange> ranges;
    for (int nodeIndex : scene.nodes) {
        GltfModel::appendNodeMesh(model, nodeIndex, glm::mat4(1.0f), vertices, ranges);
    }

    if (vertices.empty()) {
        std::cerr << "[!] glTF scene has no drawable vertices. Falling back to procedural geometry.\n";
        return;
    }

    glm::vec3 minP(vertices[0].pos[0], vertices[0].pos[1], vertices[0].pos[2]);
    glm::vec3 maxP = minP;
    for (const auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 extents = maxP - minP;
    float maxExtent = std::max(extents.x, std::max(extents.y, extents.z)) * 0.5f;
    float scale = (maxExtent > 0.0f) ? (1.0f / maxExtent) : 1.0f;
    for (auto& v : vertices) {
        glm::vec3 p(v.pos[0], v.pos[1], v.pos[2]);
        p = (p - center) * scale;
        v.pos[0] = p.x;
        v.pos[1] = p.y;
        v.pos[2] = p.z;
    }

    camera.position = {2.5f, 0.0f, 0.0f};
    camera.yaw = 180.0f;
    camera.pitch = 0.0f;
    camera.front = {-1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};

    materials.reserve(model.materials.size() + 1);
    for (const auto& mat : model.materials) {
        MaterialRuntime runtimeMat{};
        if (mat.values.find("baseColorFactor") != mat.values.end()) {
            const auto& cf = mat.values.at("baseColorFactor").ColorFactor();
            runtimeMat.params.baseColorFactor = glm::vec4(
                static_cast<float>(cf[0]),
                static_cast<float>(cf[1]),
                static_cast<float>(cf[2]),
                static_cast<float>(cf[3]));
        }
        if (mat.values.find("baseColorTexture") != mat.values.end()) {
            runtimeMat.baseColorTextureIndex = mat.values.at("baseColorTexture").TextureIndex();
            runtimeMat.params.hasBaseColorTexture = runtimeMat.baseColorTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.values.find("metallicFactor") != mat.values.end()) {
            runtimeMat.params.metallicFactor = static_cast<float>(mat.values.at("metallicFactor").Factor());
        }
        if (mat.values.find("roughnessFactor") != mat.values.end()) {
            runtimeMat.params.roughnessFactor = static_cast<float>(mat.values.at("roughnessFactor").Factor());
        }
        if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
            runtimeMat.metallicRoughnessTextureIndex = mat.values.at("metallicRoughnessTexture").TextureIndex();
            runtimeMat.params.hasMetallicRoughnessTexture = runtimeMat.metallicRoughnessTextureIndex >= 0 ? 1 : 0;
        }

        if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
            runtimeMat.normalTextureIndex = mat.additionalValues.at("normalTexture").TextureIndex();
            runtimeMat.params.hasNormalTexture = runtimeMat.normalTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("normalScale") != mat.additionalValues.end()) {
            runtimeMat.params.normalScale = static_cast<float>(mat.additionalValues.at("normalScale").Factor());
        }
        if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
            runtimeMat.occlusionTextureIndex = mat.additionalValues.at("occlusionTexture").TextureIndex();
            runtimeMat.params.hasOcclusionTexture = runtimeMat.occlusionTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("occlusionStrength") != mat.additionalValues.end()) {
            runtimeMat.params.occlusionStrength = static_cast<float>(mat.additionalValues.at("occlusionStrength").Factor());
        }
        if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
            runtimeMat.emissiveTextureIndex = mat.additionalValues.at("emissiveTexture").TextureIndex();
            runtimeMat.params.hasEmissiveTexture = runtimeMat.emissiveTextureIndex >= 0 ? 1 : 0;
        }
        if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
            const auto& ef = mat.additionalValues.at("emissiveFactor").ColorFactor();
            runtimeMat.params.emissiveFactor = glm::vec4(
                static_cast<float>(ef[0]),
                static_cast<float>(ef[1]),
                static_cast<float>(ef[2]),
                1.0f);
        }
        if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
            runtimeMat.params.alphaCutoff = static_cast<float>(mat.additionalValues.at("alphaCutoff").Factor());
        }
        if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
            const std::string mode = mat.additionalValues.at("alphaMode").string_value;
            if (mode == "MASK") runtimeMat.params.alphaMode = 1;
            else if (mode == "BLEND") runtimeMat.params.alphaMode = 2;
            else runtimeMat.params.alphaMode = 0;
        }
        materials.push_back(runtimeMat);
    }
    if (materials.empty()) {
        materials.push_back(MaterialRuntime{});
    }

    for (const auto& r : ranges) {
        DrawItem d{};
        d.firstVertex = r.firstVertex;
        d.vertexCount = r.vertexCount;
        d.materialIndex = (r.materialIndex >= 0 && r.materialIndex < static_cast<int>(materials.size()))
            ? static_cast<uint32_t>(r.materialIndex)
            : 0;
        drawItems.push_back(d);
    }
    if (drawItems.empty()) {
        drawItems.push_back({0u, static_cast<uint32_t>(vertices.size()), 0u});
    }

    loadGltfTextures();
    createFallbackTexture();

    gltfLoaded = true;
    std::cout << "[+] Loaded " << vertices.size() << " glTF vertices and " << drawItems.size()
              << " draw items from " << MODEL_PATH << std::endl;
};

State::TextureResource State::createTexture2DFromRGBA(const unsigned char* rgba, uint32_t texWidth, uint32_t texHeight, VkFormat format) {
    TextureResource out{};
    if (!rgba || texWidth == 0 || texHeight == 0) {
        return out;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = imageSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(logicalDevice, &bufferCI, nullptr, &stagingBuffer));

    VkMemoryRequirements stagingReqs{};
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingReqs.size,
        .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &stagingAlloc, nullptr, &stagingMemory));
    VK_CHECK(vkBindBufferMemory(logicalDevice, stagingBuffer, stagingMemory, 0));

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(logicalDevice, stagingMemory, 0, imageSize, 0, &mapped));
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(logicalDevice, stagingMemory);

    VkImageCreateInfo imageCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {texWidth, texHeight, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_CHECK(vkCreateImage(logicalDevice, &imageCI, nullptr, &out.image));

    VkMemoryRequirements imageReqs{};
    vkGetImageMemoryRequirements(logicalDevice, out.image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imageReqs.size,
        .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageReqs.memoryTypeBits),
    };
    VK_CHECK(vkAllocateMemory(logicalDevice, &imageAlloc, nullptr, &out.memory));
    VK_CHECK(vkBindImageMemory(logicalDevice, out.image, out.memory, 0));

    VkCommandBufferAllocateInfo cbAlloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &cbAlloc, &cb));
    VkCommandBufferBeginInfo cbBegin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cbBegin));

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = out.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {texWidth, texHeight, 1};
    vkCmdCopyBufferToImage(cb, stagingBuffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = out.image;
    toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

    VK_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cb);

    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingMemory, nullptr);

    VkImageViewCreateInfo viewCI = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VK_CHECK(vkCreateImageView(logicalDevice, &viewCI, nullptr, &out.view));

    VkSamplerCreateInfo samplerCI = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .maxAnisotropy = 1.0f,
    };
    VK_CHECK(vkCreateSampler(logicalDevice, &samplerCI, nullptr, &out.sampler));
    return out;
}

void State::destroyTexture(TextureResource& texture) {
    if (texture.sampler) vkDestroySampler(logicalDevice, texture.sampler, nullptr);
    if (texture.view) vkDestroyImageView(logicalDevice, texture.view, nullptr);
    if (texture.image) vkDestroyImage(logicalDevice, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(logicalDevice, texture.memory, nullptr);
    texture = {};
}

void State::loadGltfTextures() {
    gltfTexturesSrgb.clear();
    gltfTexturesLinear.clear();
    gltfTexturesSrgb.resize(model.textures.size());
    gltfTexturesLinear.resize(model.textures.size());
    for (size_t i = 0; i < model.textures.size(); i++) {
        const tinygltf::Texture& tex = model.textures[i];
        if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) {
            continue;
        }
        const tinygltf::Image& image = model.images[tex.source];
        if (image.image.empty() || image.component < 3) {
            continue;
        }

        std::vector<unsigned char> rgba;
        const unsigned char* src = image.image.data();
        if (image.component == 4) {
            rgba.assign(src, src + image.width * image.height * 4);
        } else {
            rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
            for (int p = 0; p < image.width * image.height; p++) {
                rgba[p * 4 + 0] = src[p * image.component + 0];
                rgba[p * 4 + 1] = src[p * image.component + 1];
                rgba[p * 4 + 2] = src[p * image.component + 2];
                rgba[p * 4 + 3] = 255;
            }
        }
        gltfTexturesSrgb[i] = createTexture2DFromRGBA(
            rgba.data(),
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            VK_FORMAT_R8G8B8A8_SRGB);
        gltfTexturesLinear[i] = createTexture2DFromRGBA(
            rgba.data(),
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            VK_FORMAT_R8G8B8A8_UNORM);
    }
}

void State::createFallbackTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    fallbackWhiteTexture = createTexture2DFromRGBA(white, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
}

void State::initShaders(){
    const std::string fragPath = "artifacts/frag.spv";
    const std::string vertPath = "artifacts/vert.spv";
    const std::string gltfFragSpv = "artifacts/gltf_frag.spv";
    const std::string gltfVertSpv = "artifacts/gltf_vert.spv";
    const std::string postFragSpv = "artifacts/postProcess_frag.spv";
    const std::string postVertSpv = "artifacts/postProcess.spv";
    std::cout << "[+] Creating Kernels" << std::endl;
    std::string cmd = "glslc \"" + static_cast<std::string>(shaderFragPath) + "\" -o \"" + fragPath + "\"";

    std::system(cmd.c_str());
    fragTs = getFileTimestamp(shaderFragPath);
    unsigned char* code;
    size_t codeSize;
    code = readFile(fragPath.c_str(), &codeSize);
    VkShaderModuleCreateInfo sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags =  0,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &fragModule));

    cmd =  "glslc \"" + static_cast<std::string>(shaderVertPath) + "\" -o \"" + vertPath + "\"";
    std::system(cmd.c_str());
    code = readFile(vertPath.c_str(), &codeSize);
    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags =  0,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &vertModule));
    free(code);

    cmd = "glslc \"" + static_cast<std::string>(gltfFragPath) + "\" -o \"" + gltfFragSpv + "\"";
    std::system(cmd.c_str());
    code = readFile(gltfFragSpv.c_str(), &codeSize);
    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &gltfFragModule));
    free(code);

    cmd = "glslc \"" + static_cast<std::string>(gltfVertPath) + "\" -o \"" + gltfVertSpv + "\"";
    std::system(cmd.c_str());
    code = readFile(gltfVertSpv.c_str(), &codeSize);
    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &gltfVertModule));
    free(code);

    cmd = "glslc \"" + static_cast<std::string>(postFragPath) + "\" -o \"" + postFragSpv + "\"";
    std::system(cmd.c_str());
    code = readFile(postFragSpv.c_str(), &codeSize);
    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &postFragModule));
    free(code);

    cmd = "glslc \"" + static_cast<std::string>(postVertPath) + "\" -o \"" + postVertSpv + "\"";
    std::system(cmd.c_str());
    code = readFile(postVertSpv.c_str(), &codeSize);
    sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &postVertModule));
    free(code);

    if(vertices.empty()) vertices = GenerateSphere(1.0, 4, 32);

    vBindingDescription = {
        .binding = 0,
        .stride = sizeof(struct Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    vAttributeDescriptions = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct Vertex, pos)
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct Vertex, color)
        },
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct Vertex, uv)
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(struct Vertex, normal)
        }
    };

    VkDeviceSize bufferSize = vertices.size() * sizeof(struct Vertex);
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
    };
    vkCreateBuffer(logicalDevice, &bCI, NULL, &stagingBuffer);
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memReqs.memoryTypeBits),
    };
    vkAllocateMemory(logicalDevice, &allocInfo, NULL, &stagingBufferMemory);
    vkBindBufferMemory(logicalDevice, stagingBuffer, stagingBufferMemory, 0);

    void* data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    VkBufferCreateInfo bCI2 = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
    };
    vkCreateBuffer(logicalDevice, &bCI2, NULL, &vertexBuffer);
    vkGetBufferMemoryRequirements(logicalDevice, vertexBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memReqs.memoryTypeBits),
    };
    vkAllocateMemory(logicalDevice, &allocInfo2, NULL, &vertexBufferMemory);
    vkBindBufferMemory(logicalDevice, vertexBuffer, vertexBufferMemory, 0);

    VkCommandBufferBeginInfo cbBI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    };
    vkBeginCommandBuffer(commandBuffers[0], &cbBI);
    VkBufferCopy copyRegion = {.size = bufferSize};
    vkCmdCopyBuffer(commandBuffers[0], stagingBuffer, vertexBuffer, 1, &copyRegion);
    vkEndCommandBuffer(commandBuffers[0]);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[0],
    };
    vkQueueSubmit(queue, 1, &si, NULL);
    vkQueueWaitIdle(queue); // safe but heavy -_-

    vkDestroyBuffer(logicalDevice, stagingBuffer, NULL);
    vkFreeMemory(logicalDevice, stagingBufferMemory, NULL);
};

void State::buildPipeline(VkShaderModule fragModule){
    std::cout << "[+] Creating Pipelines" << std::endl;
    VkPushConstantRange pcR = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .pImmutableSamplers = 0,
    };

    VkDescriptorSetLayoutCreateInfo dslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    vkCreateDescriptorSetLayout(logicalDevice, &dslCI, NULL, &uniformDescriptorSetLayout);

    VkPipelineLayoutCreateInfo plCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &uniformDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcR,
    };
    VK_CHECK(vkCreatePipelineLayout(logicalDevice, &plCI, NULL, &shaderPipelineLayout));

    VkPipelineShaderStageCreateInfo ssfCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragModule,
        .pName = "main",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo ssvCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertModule,
        .pName = "main",
        .pSpecializationInfo = NULL
    };

    VkPipelineShaderStageCreateInfo ss[] = {ssvCI, ssfCI};

    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vAttributeDescriptions.size()),
        .pVertexAttributeDescriptions = vAttributeDescriptions.data(),
    };

    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = 0,
    };

    viewport = {
        .x = 0,
        .y = 0,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    scissor = {
        .offset = {},
        .extent = {
            .width = width,
            .height = height
        }
    };

    VkPipelineViewportStateCreateInfo vsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthClampEnable = 0,
        .rasterizerDiscardEnable = 0,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = 0,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp = 0,
        .depthBiasSlopeFactor = 0,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = 0,
        .minSampleShading = 0,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = 0,
        .alphaToOneEnable = 0,
    };

    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthTestEnable = 0,
        .depthWriteEnable = 0,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = 0,
        .stencilTestEnable = 0,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkPipelineColorBlendAttachmentState colorBlend = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .logicOpEnable = 0,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlend,
        .blendConstants = {0.0f,0.0f,0.0f,0.0f}
    };

    VkPipelineDynamicStateCreateInfo dysCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .dynamicStateCount = 0,
        .pDynamicStates = NULL
    };

    VkGraphicsPipelineCreateInfo gpCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stageCount = 2,
        .pStages = ss,
        .pVertexInputState = &viCI,
        .pInputAssemblyState = &iaCI,
        .pTessellationState = NULL,
        .pViewportState = &vsCI,
        .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI,
        .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI,
        .pDynamicState = &dysCI,
        .layout = shaderPipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
        .basePipelineHandle = NULL,
        .basePipelineIndex = -1, 
    };
    VK_CHECK(vkCreateGraphicsPipelines(logicalDevice, NULL, 1, &gpCI, NULL, &shaderPipeline));
};

void State::buildGltfPipeline(){
    VkPushConstantRange pcR = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkDescriptorSetLayoutBinding frameBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo frameDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &frameBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &frameDslCI, nullptr, &uniformDescriptorSetLayout));

    std::array<VkDescriptorSetLayoutBinding, 6> materialBindings = {{
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        }
    }};
    VkDescriptorSetLayoutCreateInfo materialDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(materialBindings.size()),
        .pBindings = materialBindings.data(),
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &materialDslCI, nullptr, &materialDescriptorSetLayout));

    std::array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        uniformDescriptorSetLayout,
        materialDescriptorSetLayout
    };
    VkPipelineLayoutCreateInfo plCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(pipelineLayouts.size()),
        .pSetLayouts = pipelineLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcR,
    };
    VK_CHECK(vkCreatePipelineLayout(logicalDevice, &plCI, nullptr, &gltfPipelineLayout));

    viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    scissor = {
        .offset = {0, 0},
        .extent = {width, height}
    };

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = gltfVertModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = gltfFragModule,
            .pName = "main",
        }
    };

    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vBindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vAttributeDescriptions.size()),
        .pVertexAttributeDescriptions = vAttributeDescriptions.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vpCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    VkPipelineDynamicStateCreateInfo dynCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    VkGraphicsPipelineCreateInfo gpCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &viCI,
        .pInputAssemblyState = &iaCI,
        .pViewportState = &vpCI,
        .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI,
        .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI,
        .pDynamicState = &dynCI,
        .layout = gltfPipelineLayout,
        .renderPass = renderPass,
        .subpass = 0,
    };
    VK_CHECK(vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &gpCI, nullptr, &gltfPipeline));
}

void State::buildPostPipeline() {
    VkPushConstantRange pcR = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkDescriptorSetLayoutBinding postBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo postDslCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &postBinding,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(logicalDevice, &postDslCI, nullptr, &postDescriptorSetLayout));

    VkPipelineLayoutCreateInfo plCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &postDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcR,
    };
    VK_CHECK(vkCreatePipelineLayout(logicalDevice, &plCI, nullptr, &postPipelineLayout));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = postVertModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = postFragModule,
            .pName = "main",
        }
    };

    VkPipelineVertexInputStateCreateInfo viCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };
    VkPipelineInputAssemblyStateCreateInfo iaCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vpCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo rsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo dsCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };
    VkPipelineColorBlendAttachmentState blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    VkPipelineDynamicStateCreateInfo dynCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    };

    VkGraphicsPipelineCreateInfo gpCI = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &viCI,
        .pInputAssemblyState = &iaCI,
        .pViewportState = &vpCI,
        .pRasterizationState = &rsCI,
        .pMultisampleState = &msCI,
        .pDepthStencilState = &dsCI,
        .pColorBlendState = &cbCI,
        .pDynamicState = &dynCI,
        .layout = postPipelineLayout,
        .renderPass = postRenderPass,
        .subpass = 0,
    };
    VK_CHECK(vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &gpCI, nullptr, &postPipeline));
}

void State::initResources(){
    std::cout << "[+] Creating Resources" << std::endl;
    VkCommandPoolCreateInfo cpCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = familyIndex,
    };
    VK_CHECK(vkCreateCommandPool(logicalDevice, &cpCI, NULL, &commandPool));

    commandBuffers.resize(swapchainImages.size());
    struct VkCommandBufferAllocateInfo cbAI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 
        .pNext = NULL,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size()),
    };
    VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &cbAI, commandBuffers.data()));
};

void State::initVulkan(){
    setExtensions();
    setLayers();
    initInstance();
    initDevice();
    initFramebuffer();
    initResources();
    initGltf();
    initShaders();
    buildGltfPipeline();
    buildPostPipeline();
    initUniforms();
}

void State::initSDL(){
    SDL_SetMainReady();
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Vulkan_LoadLibrary(nullptr);
    window = SDL_CreateWindow("Shader Editor", 
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_VULKAN);
    uint32_t count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &count, NULL);
    const char* names[count];
    SDL_Vulkan_GetInstanceExtensions(window, &count, names);
    std::cout << "[+] Found SDL Extensions" << std::endl;
    for(const auto& s : names){
        std::cout << "\t" << s << std::endl;
        sdlExtensions.push_back(s);
    };
}

void State::getInput(){
    SDL_Event e;
    while(SDL_PollEvent(&e));
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if(keys[SDL_SCANCODE_ESCAPE])   {running = false;}
    if(enable_input){
    if(keys[SDL_SCANCODE_W])        {move.in = true;}
    if(keys[SDL_SCANCODE_S])        {move.out = true;}
    if(keys[SDL_SCANCODE_A])        {move.left = true;}
    if(keys[SDL_SCANCODE_D])        {move.right = true;}
    if(keys[SDL_SCANCODE_SPACE])    {move.up = true;}
    if(keys[SDL_SCANCODE_LCTRL])    {move.down = true;}

    int dx, dy;
    SDL_GetRelativeMouseState(&dx, &dy);
    camera.yaw   -= dx * 0.3;
    camera.pitch -= dy * 0.3;

    camera.pitch = glm::clamp(camera.pitch, -89.0f, 89.0f);

    camera.front.x = std::cos(glm::radians(camera.pitch)) * std::cos(glm::radians(camera.yaw));
    camera.front.y = std::cos(glm::radians(camera.pitch)) * std::sin(glm::radians(camera.yaw));
    camera.front.z = std::sin(glm::radians(camera.pitch));
    camera.front = glm::normalize(camera.front);

    glm::vec3 worldUp = {0.0f, 0.0f, 1.0f};
    glm::vec3 right = glm::cross(camera.front, worldUp);
    camera.up = glm::cross(right, camera.front);

    float velocity = 8.0f * dt;
    glm::vec3 planarFront = {};

    planarFront = worldUp * glm::dot(camera.front, worldUp);
    planarFront = camera.front - planarFront;
    if(planarFront.length() > 0.0f){
        planarFront = glm::normalize(planarFront);
    } else {
        planarFront = camera.front;
    }

    if(move.in){
        camera.position = camera.position + (planarFront * velocity); 
        move.in = false;
    }
    if(move.out){
        camera.position = camera.position + (planarFront * (-1 * velocity));
        move.out = false;
    }

    if(move.left){
        camera.position = camera.position + (right * (-1 * velocity));
        move.left = false;
    }
    if(move.right){
        camera.position = camera.position + (right * (velocity));
        move.right = false;
    }

    if(move.up){
        camera.position[2] += velocity;
        move.up = false;
    }
    if(move.down){
        camera.position[2] -= velocity;
        move.down = false;
    }
    }
}

void State::initUniforms(){
    std::cout << "[+] Initializing Uniforms" << std::endl;
    mvp = {
        {{1.0f, 0.0f, 0.0f, 0.0f}, 
         {0.0f, 1.0f, 0.0f, 0.0f}, 
         {0.0f, 0.0f, 1.0f, 0.0f}, 
         {0.0f, 0.0f, 0.0f, 1.0f}}, 

        {{1.0f, 0.0f, 0.0f, 0.0f}, 
         {0.0f, 1.0f, 0.0f, 0.0f}, 
         {0.0f, 0.0f, 1.0f, 0.0f}, 
         {0.0f, 0.0f, 0.0f, 1.0f}}, 

        {{1.0f, 0.0f, 0.0f, 0.0f}, 
         {0.0f, 1.0f, 0.0f, 0.0f}, 
         {0.0f, 0.0f, 1.0f, 0.0f}, 
         {0.0f, 0.0f, 0.0f, 1.0f}},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };

    VkBufferCreateInfo bCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = sizeof(MVP),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
    };
    uniformBuffers.resize(swapchainImages.size());
    uniformBufferMemory.resize(swapchainImages.size());
    uniformBufferMapped.resize(swapchainImages.size());
    for(size_t i{0u}; i < uniformBuffers.size(); i++){
        VkMemoryRequirements uniformMemReqs;
        vkCreateBuffer(logicalDevice, &bCI, NULL, &uniformBuffers[i]);
        vkGetBufferMemoryRequirements(logicalDevice, uniformBuffers[i], &uniformMemReqs);
        VkMemoryAllocateInfo memAI = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = NULL,
            .allocationSize = uniformMemReqs.size,
            .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                    uniformMemReqs.memoryTypeBits)
        };
        vkAllocateMemory(logicalDevice, &memAI, NULL, &uniformBufferMemory[i]);
        vkBindBufferMemory(logicalDevice, uniformBuffers[i], uniformBufferMemory[i], 0);
        vkMapMemory(logicalDevice, uniformBufferMemory[i], 0, sizeof(MVP), 0, &uniformBufferMapped[i]);
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = static_cast<uint32_t>(swapchainImages.size() + materials.size()),
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = static_cast<uint32_t>(5 * std::max<size_t>(1, materials.size()) + swapchainImages.size()),
        }
    }};

    VkDescriptorPoolCreateInfo poolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = static_cast<uint32_t>(2 * swapchainImages.size() + std::max<size_t>(1, materials.size())),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vkCreateDescriptorPool(logicalDevice, &poolCI, NULL, &descriptorPool);

    std::vector<VkDescriptorSetLayout> layouts(swapchainImages.size(), uniformDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(swapchainImages.size()),
        .pSetLayouts = layouts.data()
    };
    descriptorSets.resize(swapchainImages.size());
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, descriptorSets.data());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = uniformBuffers[i],
            .offset = 0,
            .range  = sizeof(MVP),
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = NULL,
            .dstSet = descriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = NULL,
            .pBufferInfo = &bufferInfo,
            .pTexelBufferView = NULL,
        };
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, NULL);
    }

    createMaterialResources();

    std::vector<VkDescriptorSetLayout> postLayouts(swapchainImages.size(), postDescriptorSetLayout);
    VkDescriptorSetAllocateInfo postAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(postLayouts.size()),
        .pSetLayouts = postLayouts.data(),
    };
    postDescriptorSets.resize(swapchainImages.size());
    VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &postAllocInfo, postDescriptorSets.data()));

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkDescriptorImageInfo imageInfo = {
            .sampler = offscreenColorSampler,
            .imageView = offscreenColorImageViews[i],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = postDescriptorSets[i],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo,
        };
        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
    }
}

void State::createMaterialResources() {
    if (materials.empty()) {
        materials.push_back(MaterialRuntime{});
    }

    materialUniformBuffers.resize(materials.size());
    materialUniformBufferMemory.resize(materials.size());
    materialDescriptorSets.resize(materials.size());

    VkBufferCreateInfo bufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(MaterialParams),
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    for (size_t i = 0; i < materials.size(); i++) {
        VkMemoryRequirements req{};
        VK_CHECK(vkCreateBuffer(logicalDevice, &bufferCI, nullptr, &materialUniformBuffers[i]));
        vkGetBufferMemoryRequirements(logicalDevice, materialUniformBuffers[i], &req);
        VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = findMemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, req.memoryTypeBits),
        };
        VK_CHECK(vkAllocateMemory(logicalDevice, &alloc, nullptr, &materialUniformBufferMemory[i]));
        VK_CHECK(vkBindBufferMemory(logicalDevice, materialUniformBuffers[i], materialUniformBufferMemory[i], 0));
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(logicalDevice, materialUniformBufferMemory[i], 0, sizeof(MaterialParams), 0, &mapped));
        std::memcpy(mapped, &materials[i].params, sizeof(MaterialParams));
        vkUnmapMemory(logicalDevice, materialUniformBufferMemory[i]);
    }

    std::vector<VkDescriptorSetLayout> layouts(materials.size(), materialDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data(),
    };
    VK_CHECK(vkAllocateDescriptorSets(logicalDevice, &allocInfo, materialDescriptorSets.data()));

    for (size_t i = 0; i < materials.size(); i++) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = materialUniformBuffers[i],
            .offset = 0,
            .range = sizeof(MaterialParams),
        };

        auto pickTexture = [&](int texIndex, bool srgb) -> const TextureResource* {
            const auto& table = srgb ? gltfTexturesSrgb : gltfTexturesLinear;
            if (texIndex >= 0 && texIndex < static_cast<int>(table.size()) && table[texIndex].view != VK_NULL_HANDLE) {
                return &table[texIndex];
            }
            return &fallbackWhiteTexture;
        };

        const TextureResource* baseColorTex = pickTexture(materials[i].baseColorTextureIndex, true);
        const TextureResource* metallicRoughnessTex = pickTexture(materials[i].metallicRoughnessTextureIndex, false);
        const TextureResource* normalTex = pickTexture(materials[i].normalTextureIndex, false);
        const TextureResource* occlusionTex = pickTexture(materials[i].occlusionTextureIndex, false);
        const TextureResource* emissiveTex = pickTexture(materials[i].emissiveTextureIndex, true);

        std::array<VkDescriptorImageInfo, 5> imageInfos = {{
            { baseColorTex->sampler, baseColorTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { metallicRoughnessTex->sampler, metallicRoughnessTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { normalTex->sampler, normalTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { occlusionTex->sampler, occlusionTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { emissiveTex->sampler, emissiveTex->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
        }};

        std::array<VkWriteDescriptorSet, 6> writes = {{
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[0],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[1],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 3,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[2],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 4,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[3],
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = materialDescriptorSets[i],
                .dstBinding = 5,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfos[4],
            }
        }};
        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

static void printMat4(const glm::mat4& m)
{
    for (int r = 0; r < 4; ++r) {
        std::cout << "[ ";
        for (int c = 0; c < 4; ++c) {
            std::cout << m[c][r];
            if (c < 3) std::cout << ", ";
        }
        std::cout << " ]\n";
    }
}

void printMVP(const MVP& mvp){
    std::cout << "Model:\n";
    printMat4(mvp.model);

    std::cout << "\nView:\n";
    printMat4(mvp.view);

    std::cout << "\nProjection:\n";
    printMat4(mvp.proj);
}

void State::updateUniforms(){
    if(enable_input){
    mvp.model = glm::mat4(1.0);
    if (gltfLoaded) {
        float angle = glm::radians(90.0f);
        glm::vec3 axis = {1.0f, 0.0f, 0.0f};
        mvp.model = glm::rotate(mvp.model, angle, axis);
    }

    glm::vec3 worldUp = {0.0, 0.0, 1.0};
    glm::vec3 right = glm::normalize(glm::cross(camera.front, worldUp));
    camera.up = glm::cross(right, camera.front);

    glm::vec3 center = camera.position + camera.front;
    mvp.view = glm::lookAt(camera.position, center, camera.up);

    float fovy = glm::radians(90.0f);
    float aspect = (float)width / (float)height;
    float nearZ = 0.001f;
    float farZ = 100.0f;
    mvp.proj = glm::perspective(fovy, aspect, nearZ, farZ);
    mvp.proj[1][1] *= -1;
    mvp.camPos = glm::vec4(camera.position, 1.0f);
    } else {
    mvp.model = glm::mat4(1.0);
    mvp.view  = glm::mat4(1.0);
    mvp.proj  = glm::mat4(1.0);
    mvp.camPos = glm::vec4(0.0f, 0.0f, 2.5f, 1.0f);
    }
    memcpy(uniformBufferMapped[frameIndex], &mvp, sizeof(mvp));
    //printMVP(mvp);
}

void State::runRenderPass(uint32_t imgIdx, VkPipeline pipeline){
    VkCommandBufferBeginInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL,
    };
    VkClearValue clearColor[2] = {0};
    clearColor[0].color = (VkClearColorValue){{0.05f, 0.15f, 0.20f, 1.0f}};
    clearColor[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpBI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = NULL,
        .renderPass = renderPass,
        .framebuffer = framebuffers[imgIdx],
        .renderArea = {{0,0},{width, height}},
        .clearValueCount = sizeof(clearColor)/sizeof(VkClearValue),
        .pClearValues = clearColor
    };

    vkBeginCommandBuffer(commandBuffers[frameIndex], &cbCI);
    vkCmdBeginRenderPass(commandBuffers[frameIndex], &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    struct PushConstants pc = { {static_cast<float>(width), static_cast<float>(height)}, runtime.count(), 0.0f, };
    vkCmdPushConstants(commandBuffers[frameIndex], shaderPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
            0, sizeof(PushConstants), &pc);

    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 1, &vertexBuffer, offsets);
    vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, shaderPipelineLayout,
            0, 1, &descriptorSets[frameIndex], 0, NULL);
    vkCmdDraw(commandBuffers[frameIndex], vertices.size(), 1, 0, 0);

    vkCmdEndRenderPass(commandBuffers[frameIndex]);
    vkEndCommandBuffer(commandBuffers[frameIndex]);
}

void State::runRenderPassGltf(uint32_t imgIdx){
    VkCommandBufferBeginInfo cbCI = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    VkClearValue sceneClear[2] = {0};
    sceneClear[0].color = (VkClearColorValue){{0.00f, 0.00f, 0.00f, 1.0f}};
    sceneClear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo sceneRpBI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass,
        .framebuffer = framebuffers[imgIdx],
        .renderArea = {{0,0},{width, height}},
        .clearValueCount = sizeof(sceneClear)/sizeof(VkClearValue),
        .pClearValues = sceneClear
    };

    VkClearValue postClear = {};
    postClear.color = (VkClearColorValue){{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo postRpBI = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = postRenderPass,
        .framebuffer = postFramebuffers[imgIdx],
        .renderArea = {{0,0},{width, height}},
        .clearValueCount = 1,
        .pClearValues = &postClear
    };

    vkBeginCommandBuffer(commandBuffers[frameIndex], &cbCI);
    vkCmdBeginRenderPass(commandBuffers[frameIndex], &sceneRpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipeline);

    PushConstants pc = {{static_cast<float>(width), static_cast<float>(height)}, runtime.count(), 0.0f};
    vkCmdPushConstants(commandBuffers[frameIndex], gltfPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 1, &vertexBuffer, offsets);
    vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipelineLayout,
            0, 1, &descriptorSets[frameIndex], 0, nullptr);

    for (const auto& draw : drawItems) {
        if (draw.materialIndex >= materialDescriptorSets.size()) {
            continue;
        }
        vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipelineLayout,
                1, 1, &materialDescriptorSets[draw.materialIndex], 0, nullptr);
        vkCmdDraw(commandBuffers[frameIndex], draw.vertexCount, 1, draw.firstVertex, 0);
    }

    vkCmdEndRenderPass(commandBuffers[frameIndex]);

    vkCmdBeginRenderPass(commandBuffers[frameIndex], &postRpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
    vkCmdPushConstants(commandBuffers[frameIndex], postPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
    vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout,
            0, 1, &postDescriptorSets[imgIdx], 0, nullptr);
    vkCmdDraw(commandBuffers[frameIndex], 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffers[frameIndex]);

    vkEndCommandBuffer(commandBuffers[frameIndex]);
}

void State::rebuildFragShader(){
    const std::string fragPath = "artifacts/frag.spv";
    std::cout << "[+] Rebuilding Frag ... " << std::endl;
    std::string cmd = "glslc \"" + static_cast<std::string>(shaderFragPath) + "\" -o \"" + fragPath + "\"";

    uint32_t res = std::system(cmd.c_str());
    if(res != 0){
        std::cout << "[!] Failed to build Frag" << std::endl;
        return;
    };

    fragTs = getFileTimestamp(shaderFragPath);
    unsigned char* code;
    size_t codeSize;
    code = readFile(fragPath.c_str(), &codeSize);
    VkShaderModuleCreateInfo sCI = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags =  0,
        .codeSize = codeSize,
        .pCode = (const uint32_t*)code,
    };
    VK_CHECK(vkCreateShaderModule(logicalDevice, &sCI, NULL, &fragModule));

    buildPipeline(fragModule);
};

void State::appLogic(){
    getInput();
    if (gltfLoaded) {
        return;
    }
    std::filesystem::file_time_type shaderTs = getFileTimestamp(shaderFragPath);
    if(shaderTs != std::filesystem::file_time_type::min() && shaderTs > fragTs){
        std::cout << "file has been written" << std::endl;
        fragTs = shaderTs;
        rebuildFragShader();
    }
};

void State::renderLoop(){
    std::cout << "[+] Entering RenderLoop" << std::endl;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    while(running){
        tStart = std::chrono::steady_clock::now();
        appLogic();

        vkWaitForFences(logicalDevice, 1, &fences[frameIndex], VK_TRUE, UINT64_MAX);
        uint32_t imgIdx;
        vkAcquireNextImageKHR(logicalDevice, swapchain, UINT64_MAX, imageAvailableSemaphores[frameIndex], VK_NULL_HANDLE, &imgIdx);
        vkResetFences(logicalDevice, 1, &fences[frameIndex]);
        vkResetCommandBuffer(commandBuffers[frameIndex], 0);

        updateUniforms();
        runRenderPassGltf(imgIdx);

        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = 0,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &imageAvailableSemaphores[frameIndex],
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffers[frameIndex],
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderCompleteSemaphores[imgIdx], 
        };
        vkQueueSubmit(queue, 1, &submitInfo, fences[frameIndex]);

        VkPresentInfoKHR presentInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderCompleteSemaphores[imgIdx],
            .swapchainCount = 1,
            .pSwapchains = &swapchain, 
            .pImageIndices = &imgIdx,
            .pResults = NULL
        };
        vkQueuePresentKHR(queue, &presentInfo);
        frameIndex = (frameIndex + 1) % swapchainImages.size();

        tEnd = std::chrono::steady_clock::now();
        std::chrono::duration<float> delta = tEnd - tStart;
        runtime += delta;
        if(delta < frameTime){
            std::this_thread::sleep_for(frameTime - delta);
            runtime+=(frameTime - delta);
            dt = frameTime.count();
            std::cout << 1 / (frameTime - delta).count() << '\r';
        } else { dt = delta.count(); std::cout << 1 / delta.count() << '\r'; }
    }
    vkDeviceWaitIdle(logicalDevice);

};

int main(){
    State state;
    state.initSDL();
    state.initVulkan();
    state.renderLoop();
    state.exit();
}

void State::exit(){
    for(size_t i{0u}; i<swapchainImages.size(); i++){
        vkDestroyFramebuffer(logicalDevice, framebuffers[i], NULL);
        vkDestroyFramebuffer(logicalDevice, postFramebuffers[i], NULL);
        vkDestroyImageView(logicalDevice, swapchainImageViews[i], NULL);
        vkDestroyImageView(logicalDevice, offscreenColorImageViews[i], NULL);
        vkDestroyImage(logicalDevice, offscreenColorImages[i], NULL);
        vkFreeMemory(logicalDevice, offscreenColorImageMemory[i], NULL);
        vkDestroyImageView(logicalDevice, depthImageViews[i], NULL);
        vkDestroyImage(logicalDevice, depthImages[i], NULL);
        vkFreeMemory(logicalDevice, depthImageMemory[i], NULL);
        vkDestroyFence(logicalDevice, fences[i], NULL);
        vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], NULL);
        vkDestroySemaphore(logicalDevice, renderCompleteSemaphores[i], NULL);
        vkUnmapMemory(logicalDevice, uniformBufferMemory[i]);
        vkDestroyBuffer(logicalDevice, uniformBuffers[i], nullptr);
        vkFreeMemory(logicalDevice, uniformBufferMemory[i], nullptr);
    };
    vkDestroySwapchainKHR(logicalDevice, swapchain, NULL);
    SDL_DestroyWindow(window);

    for (size_t i = 0; i < materialUniformBuffers.size(); i++) {
        if (materialUniformBuffers[i]) vkDestroyBuffer(logicalDevice, materialUniformBuffers[i], nullptr);
        if (materialUniformBufferMemory[i]) vkFreeMemory(logicalDevice, materialUniformBufferMemory[i], nullptr);
    }
    for (auto& t : gltfTexturesSrgb) {
        destroyTexture(t);
    }
    for (auto& t : gltfTexturesLinear) {
        destroyTexture(t);
    }
    destroyTexture(fallbackWhiteTexture);

    vkDestroyBuffer(logicalDevice, vertexBuffer, NULL);
    vkFreeMemory(logicalDevice, vertexBufferMemory, NULL);

    vkDestroyPipeline(logicalDevice, shaderPipeline, NULL);
    vkDestroyPipeline(logicalDevice, gltfPipeline, NULL);
    vkDestroyPipeline(logicalDevice, postPipeline, NULL);
    vkDestroyPipelineLayout(logicalDevice, shaderPipelineLayout, NULL);
    vkDestroyPipelineLayout(logicalDevice, gltfPipelineLayout, NULL);
    vkDestroyPipelineLayout(logicalDevice, postPipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(logicalDevice, materialDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, postDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, uniformDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
    vkDestroySampler(logicalDevice, offscreenColorSampler, nullptr);

    vkDestroyRenderPass(logicalDevice, renderPass, NULL);
    vkDestroyRenderPass(logicalDevice, postRenderPass, NULL);
    vkDestroyShaderModule(logicalDevice, fragModule, NULL);
    vkDestroyShaderModule(logicalDevice, vertModule, NULL);
    vkDestroyShaderModule(logicalDevice, gltfFragModule, NULL);
    vkDestroyShaderModule(logicalDevice, gltfVertModule, NULL);
    vkDestroyShaderModule(logicalDevice, postFragModule, NULL);
    vkDestroyShaderModule(logicalDevice, postVertModule, NULL);

    vkFreeCommandBuffers(logicalDevice, commandPool, commandBuffers.size(), commandBuffers.data());
    vkDestroyCommandPool(logicalDevice, commandPool, NULL);

    vkDestroyDevice(logicalDevice, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);

    vkDestroyInstance(instance, NULL);
    SDL_Quit();

    std::cout << std::endl << "exit(0)" << std::endl;
};
