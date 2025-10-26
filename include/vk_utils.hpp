#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include "materials.hpp"

#ifndef VK_CHECK
#define VK_CHECK(call)                                                     \
    do {                                                                   \
        VkResult _vk_result_ = (call);                                     \
        if (_vk_result_ != VK_SUCCESS) {                                   \
            std::fprintf(stderr,                                           \
                "[VK_CHECK] %s:%d: %s failed with VkResult = %d\n",        \
                __FILE__, __LINE__, #call, (int)_vk_result_);              \
            std::abort();                                                  \
        }                                                                  \
    } while (0)
#endif

#ifndef VK_CHECK_RET
// Same as VK_CHECK but returns false from the current function instead of aborting
#define VK_CHECK_RET(call)                                                 \
    do {                                                                   \
        VkResult _vk_result_ = (call);                                     \
        if (_vk_result_ != VK_SUCCESS) {                                   \
            std::fprintf(stderr,                                           \
                "[VK_CHECK_RET] %s:%d: %s failed with VkResult = %d\n",    \
                __FILE__, __LINE__, #call, (int)_vk_result_);              \
            return false;                                                  \
        }                                                                  \
    } while (0)
#endif

struct VulkanContext {
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debugMessenger{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    uint32_t graphicsQueueFamily = 0;
    uint32_t presentQueueFamily = 0;
    VkQueue graphicsQueue{};
    VkQueue  presentQueue{};
    VkSurfaceKHR surface{};

    // Swapchain
    VkSwapchainKHR swapchain{};
    VkFormat swapchainFormat{};
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    // Minimal render stuff
    VkRenderPass renderPass{};
    std::vector<VkFramebuffer> framebuffers;

    uint32_t currentFrame = 0;

    VkCommandPool commandPool{};
    std::vector<VkCommandBuffer> commandBuffers;

    VkSemaphore imageAvailableSemaphore{};
    VkSemaphore renderFinishedSemaphore{};
    VkFence inFlightFence{};

    // Pipeline
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};

    // Mesh buffers
    VkBuffer vertexBuffer{};
    VkDeviceMemory vertexMemory{};
    VkBuffer indexBuffer{};
    VkDeviceMemory indexMemory{};
    uint32_t indexCount = 0;

    // Voxel pipeline
    VkPipeline voxelPipeline{};
    VkPipelineLayout voxelPipelineLayout{};

    // Texture atlas resources
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    VkImage        atlasImage{};
    VkDeviceMemory atlasMemory{};
    VkImageView    atlasView{};
    VkSampler      atlasSampler{};

    // Descriptor set layout/pool/set
    VkDescriptorSetLayout descSetLayout{};
    VkDescriptorPool      descPool{};
    VkDescriptorSet       descSet{};

    // materials UBO
    VkBuffer       materialUBO = {};
    VkDeviceMemory materialUBOMem = {};
    uint32_t       materialUBOSize = 0;

    // Depth resources
    VkFormat      depthFormat{};
    VkImage       depthImage{};
    VkDeviceMemory depthMemory{};
    VkImageView   depthView{};

    float maxSamplerAnisotropy = 1.0f;
    float currentAniso = 1.f;
    bool anisotropyFeature = false;
};

struct RegionGPU {
    VkBuffer       vbo = VK_NULL_HANDLE;
    VkDeviceMemory vmem = VK_NULL_HANDLE;
    VkBuffer       ibo = VK_NULL_HANDLE;
    VkDeviceMemory imem = VK_NULL_HANDLE;
    uint32_t       indexCount = 0;
};

// New helpers:
bool createSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height);
bool createImageViews(VulkanContext& ctx);
bool createRenderPass(VulkanContext& ctx);
bool createFramebuffers(VulkanContext& ctx);
bool createCommandPoolAndBuffers(VulkanContext& ctx);
bool createSyncObjects(VulkanContext& ctx);
using DrawSceneFn = std::function<void(VkCommandBuffer)>;
bool recordCommandBuffers(VulkanContext& ctx, float r, float g, float b, const float* mvp, DrawSceneFn drawScene);
bool drawFrame(VulkanContext& ctx);
bool drawFrameWithMVP(VulkanContext& ctx, const float* mvp, DrawSceneFn drawScene);
void cleanupSwapchain(VulkanContext& ctx);
bool createInstance(VulkanContext& ctx, const char* appName, bool enableValidation);
void setupDebug(VulkanContext& ctx);
void destroyDebug(VulkanContext& ctx);
bool pickPhysicalDevice(VulkanContext& ctx);
bool createDevice(VulkanContext& ctx);
static bool findQueueFamilies(VulkanContext& ctx, VkPhysicalDevice dev);
VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
std::vector<char> readFile(const std::string& path);
bool createPipeline(VulkanContext& ctx, const std::string& shaderDir);
void destroyPipeline(VulkanContext& ctx);
uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props);
bool createBuffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);
bool copyBuffer(VulkanContext& ctx, VkBuffer src, VkBuffer dst, VkDeviceSize size);
bool uploadVoxelMesh(VulkanContext& ctx, const std::vector<float>& verts,
    const std::vector<uint32_t>& indices);
void destroyVoxelMesh(VulkanContext& ctx);

bool createVoxelPipeline(VulkanContext& ctx, const std::string& shaderDir);
void destroyVoxelPipeline(VulkanContext& ctx);
// Images & textures
bool createImage(VulkanContext& ctx, uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags props,
    uint32_t mipLevels, VkImage& image, VkDeviceMemory& mem);
bool transitionImageLayoutRange(VulkanContext& ctx, VkImage image, VkFormat fmt,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    uint32_t baseMip, uint32_t levelCount);
bool copyBufferToImage(VulkanContext& ctx, VkBuffer src, VkImage dst, uint32_t w, uint32_t h);
bool createTextureAtlasFromFile(VulkanContext& ctx, const char* path);

// Descriptors
bool createDescriptors(VulkanContext& ctx);
void destroyDescriptors(VulkanContext& ctx);

// Depth helpers
VkFormat findSupportedFormat(VkPhysicalDevice phys,
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features);
VkFormat findDepthFormat(VulkanContext& ctx);
bool hasStencilComponent(VkFormat format);

bool createDepthResources(VulkanContext& ctx, uint32_t width, uint32_t height);
void destroyDepthResources(VulkanContext& ctx);

bool recreateAtlasSampler(VulkanContext& ctx, float anisoLevel);

// Upload a region mesh (creates new GPU buffers and fills them). Destroys old buffers if present.
bool uploadRegionMesh(struct VulkanContext& ctx,
    RegionGPU& dst,
    const std::vector<float>& vertices,
    const std::vector<uint32_t>& indices);

// Destroy one region's GPU buffers (safe to call on empty RegionGPU).
void destroyRegionBuffers(struct VulkanContext& ctx, RegionGPU& rgn);

bool createMaterialUBO(VulkanContext& ctx);
void destroyMaterialUBO(VulkanContext& ctx);