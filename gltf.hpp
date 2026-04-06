#pragma once
#include "external/tiny_gltf.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "lib.hpp"

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

struct PrimitiveRange {
    uint32_t firstVertex{0};
    uint32_t vertexCount{0};
    int materialIndex{-1};
};

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

struct GltfModel{
    const char* gltfFragPath = "kernels/gltf.frag";
    const char* gltfVertPath = "kernels/gltf.vert";
    const char* postFragPath = "kernels/postProcess.frag";
    const char* postVertPath = "kernels/postProcess.vert";

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    bool ready = false;
    std::vector<Vertex> vertices;
    uint32_t width = 640;
    uint32_t height = 640;
    struct {
       glm::vec3 position = {};
       glm::vec3 front = {};
       glm::vec3 up = {};
       float yaw = 0.0;
       float pitch = 0.0;
    } camera;

    VkDevice logicalDevice;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    VkQueue queue;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkCommandPool commandPool;
    VkRenderPass postRenderPass{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkBuffer vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};

    VkVertexInputBindingDescription vBindingDescription;
    std::vector<VkVertexInputAttributeDescription> vAttributeDescriptions;

    TextureResource fallbackWhiteTexture{};
    std::vector<TextureResource> gltfTexturesSrgb;
    std::vector<TextureResource> gltfTexturesLinear;
    std::vector<MaterialRuntime> materials;
    std::vector<DrawItem> drawItems;
    
    std::vector<VkBuffer> materialUniformBuffers;
    std::vector<VkDeviceMemory> materialUniformBufferMemory;
    VkDescriptorSetLayout materialDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout postDescriptorSetLayout{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> materialDescriptorSets;
    std::vector<VkDescriptorSet> postDescriptorSets;

    VkDescriptorSetLayout uniformDescriptorSetLayout;
    VkViewport viewport;
    VkRect2D scissor;

    VkPipelineLayout gltfPipelineLayout{VK_NULL_HANDLE};
    VkPipelineLayout postPipelineLayout{VK_NULL_HANDLE};
    VkPipeline gltfPipeline{VK_NULL_HANDLE};
    VkPipeline postPipeline{VK_NULL_HANDLE};

    VkShaderModule gltfFragModule{VK_NULL_HANDLE};
    VkShaderModule gltfVertModule{VK_NULL_HANDLE};
    VkShaderModule postFragModule{VK_NULL_HANDLE};
    VkShaderModule postVertModule{VK_NULL_HANDLE};

    std::vector<VkDeviceMemory> offscreenColorImageMemory;
    std::vector<VkFramebuffer> sceneFramebuffers;
    std::vector<VkFramebuffer> postFramebuffers;
    std::vector<VkImageView> offscreenColorImageViews;
    std::vector<VkImage> offscreenColorImages;
    VkSampler offscreenColorSampler{VK_NULL_HANDLE};

    std::vector<VkBuffer> frameUniformBuffers;
    std::vector<VkDeviceMemory> frameUniformBufferMemory;
    std::vector<void*> frameUniformMapped;
    std::vector<VkDescriptorSet> frameDescriptorSets;

    ~GltfModel() = default;
    void deleteGLTF();


    void initGltf(std::string path, VkDevice LogicalDevice, VkQueue& Queue, VkPhysicalDeviceMemoryProperties& memoryProperties, VkCommandPool& commandPool, uint32_t Width, uint32_t Height);
    void createResources(const std::vector<VkImage>& swapchainImages, VkFormat swapchainImageFormat, VkRenderPass renderPass,
                         const std::vector<VkImageView>& swapchainImageViews, const std::vector<VkImageView>& depthImageViews);
    void updateBuffers(uint32_t frameIndex, const MVP& mvp);
    void recordRenderPass(uint32_t imgIdx, uint32_t frameIndex, std::vector<VkCommandBuffer>& commandBuffers, std::chrono::duration<float>& runtime);
    void initFrameResources(uint32_t frameCount);
    void createImages(const std::vector<VkImage>& swapchainImages, VkFormat swapchainImageFormat, VkRenderPass RenderPass,
                      const std::vector<VkImageView>& swapchainImageViews, const std::vector<VkImageView>& depthImageViews);
    void initShaders();
    void buildGltfPipeline();
    void buildPostPipeline();
    void createVertexBuffer();
    TextureResource createTexture2DFromRGBA(const unsigned char* rgba, uint32_t width, uint32_t height, VkFormat format);
    void loadGltfTextures();
    void destroyTexture(TextureResource& texture);
    void createFallbackTexture();
    void createMaterialResources();
    glm::mat4 nodeLocalMatrix(const tinygltf::Node& node);
    void appendNodeMesh(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix, std::vector<Vertex>& outVertices,
                        std::vector<PrimitiveRange>& outRanges);
    void appendPrimitiveVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const glm::mat4& worldMatrix, 
                                 std::vector<Vertex>& outVertices, std::vector<PrimitiveRange>& outRanges);
};

