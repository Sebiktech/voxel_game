#include "vk_utils.hpp"
#include "third_party/stb_image.h"
#include <stdexcept>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>

// Choose swapchain format
static VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}

bool createSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice, ctx.surface, &caps);

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physicalDevice, ctx.surface, &count, formats.data());
    if (formats.empty()) return false;

    VkSurfaceFormatKHR surf = chooseSurfaceFormat(formats);

    VkExtent2D extent{};
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    }
    else {
        extent = { std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width),
                   std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height) };
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface = ctx.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = surf.format;
    ci.imageColorSpace = surf.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t qIndex = ctx.graphicsQueueFamily;
    if (ctx.graphicsQueueFamily != ctx.presentQueueFamily) {
        uint32_t idx[2] = { ctx.graphicsQueueFamily, ctx.presentQueueFamily };
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = idx;
    }
    else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.device, &ci, nullptr, &ctx.swapchain) != VK_SUCCESS) return false;

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imgCount, nullptr);
    ctx.swapchainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imgCount, ctx.swapchainImages.data());

    ctx.swapchainFormat = surf.format;
    ctx.swapchainExtent = extent;
    return true;
}

bool createImageViews(VulkanContext& ctx) {
    ctx.swapchainImageViews.resize(ctx.swapchainImages.size());
    for (size_t i = 0; i < ctx.swapchainImages.size(); ++i) {
        VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        iv.image = ctx.swapchainImages[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = ctx.swapchainFormat;
        iv.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel = 0;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount = 1;
        if (vkCreateImageView(ctx.device, &iv, nullptr, &ctx.swapchainImageViews[i]) != VK_SUCCESS) return false;
    }
    return true;
}

bool createRenderPass(VulkanContext& ctx) {
    // COLOR
    VkAttachmentDescription color{};
    color.format = ctx.swapchainFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // DEPTH (we only need the format known here)
    VkAttachmentDescription depth{};
    depth.format = findDepthFormat(ctx); // sets ctx.depthFormat
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkAttachmentDescription attachments[2] = { color, depth };

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 2;                 // <-- TWO
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;

    return vkCreateRenderPass(ctx.device, &rpci, nullptr, &ctx.renderPass) == VK_SUCCESS;
}

bool createFramebuffers(VulkanContext& ctx) {
    // Sanity checks
    if (!ctx.renderPass) { std::cerr << "[VK] FB: renderPass is null\n"; return false; }
    if (ctx.swapchainImageViews.empty()) { std::cerr << "[VK] FB: no swapchain image views\n"; return false; }
    if (!ctx.depthView) { std::cerr << "[VK] FB: depthView is null\n"; return false; }

    ctx.framebuffers.resize(ctx.swapchainImageViews.size());
    for (size_t i = 0; i < ctx.swapchainImageViews.size(); ++i) {
        VkImageView attachments[2] = {
            ctx.swapchainImageViews[i], // color
            ctx.depthView               // depth
        };

        VkFramebufferCreateInfo fbi{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbi.renderPass = ctx.renderPass;
        fbi.attachmentCount = 2;                // must match render pass
        fbi.pAttachments = attachments;
        fbi.width = ctx.swapchainExtent.width;
        fbi.height = ctx.swapchainExtent.height;
        fbi.layers = 1;

        VkResult r = vkCreateFramebuffer(ctx.device, &fbi, nullptr, &ctx.framebuffers[i]);
        if (r != VK_SUCCESS) {
            std::cerr << "[VK] vkCreateFramebuffer failed at i=" << i
                << " result=" << (int)r
                << " colorView=" << (void*)attachments[0]
                << " depthView=" << (void*)attachments[1]
                << " size=" << fbi.width << "x" << fbi.height << "\n";
            return false;
        }
    }
    return true;
}

bool createCommandPoolAndBuffers(VulkanContext& ctx) {
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = ctx.graphicsQueueFamily;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx.device, &pci, nullptr, &ctx.commandPool) != VK_SUCCESS) return false;

    ctx.commandBuffers.resize(ctx.framebuffers.size());
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)ctx.commandBuffers.size();
    return vkAllocateCommandBuffers(ctx.device, &ai, ctx.commandBuffers.data()) == VK_SUCCESS;
}

// Records all CBs once with the given MVP (useful for static scenes)
// If you’re drawing every frame with a changing camera, prefer drawFrameWithMVP below.
bool recordCommandBuffers(VulkanContext& ctx, float r, float g, float b, const float* mvp, DrawSceneFn drawScene) {
    for (size_t i = 0; i < ctx.commandBuffers.size(); ++i) {
        VkCommandBuffer cb = ctx.commandBuffers[i];

        // (re)start this command buffer cleanly
        vkResetCommandBuffer(cb, 0);

        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return false;

        VkClearValue clears[2]{};
        clears[0].color = {{r, g, b, 1.0f}};
        clears[1].depthStencil = { 1.0f, 0 };            // depth=1.0 (far), stencil unused

        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = ctx.renderPass;
        rp.framebuffer = ctx.framebuffers[i];
        rp.renderArea.offset = { 0, 0 };
        rp.renderArea.extent = ctx.swapchainExtent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;

        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

        // 1) bind pipeline
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.voxelPipeline);

        // 2) bind descriptor set 0 with the SAME pipeline layout
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
            ctx.voxelPipelineLayout, 0, 1, &ctx.descSet, 0, nullptr);

        // 3) push constants: mat4 (64B) + atlasScale(2) + atlasOffset(2) = 80B
        float pcData[20];
        memcpy(pcData, mvp, sizeof(float) * 16);
        pcData[16] = 1.0f / 4.0f;                // atlasScale.x
        pcData[17] = 1.0f / 4.0f;                // atlasScale.y
        pcData[18] = 1.0f / ctx.atlasWidth;      // atlasTexel.x  (add these two!)
        pcData[19] = 1.0f / ctx.atlasHeight;     // atlasTexel.y

        vkCmdPushConstants(cb, ctx.voxelPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(float) * 20, pcData);

        // 4) bind buffers + draw
        if (drawScene) {
            drawScene(cb); // e.g. world.draw(ctx, cb)
        }

        vkCmdEndRenderPass(cb);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS) return false;
    }
    return true;
}

bool createSyncObjects(VulkanContext& ctx) {
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateSemaphore(ctx.device, &si, nullptr, &ctx.imageAvailableSemaphore) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(ctx.device, &si, nullptr, &ctx.renderFinishedSemaphore) != VK_SUCCESS) return false;
    if (vkCreateFence(ctx.device, &fi, nullptr, &ctx.inFlightFence) != VK_SUCCESS) return false;
    return true;
}

bool drawFrame(VulkanContext& ctx) {
    vkWaitForFences(ctx.device, 1, &ctx.inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx.device, 1, &ctx.inFlightFence);

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(ctx.device, ctx.swapchain, UINT64_MAX,
        ctx.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acq != VK_SUCCESS) return false;

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &ctx.imageAvailableSemaphore;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &ctx.commandBuffers[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &ctx.renderFinishedSemaphore;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &submit, ctx.inFlightFence) != VK_SUCCESS) return false;

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &ctx.renderFinishedSemaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &ctx.swapchain;
    present.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(ctx.graphicsQueue, &present) == VK_SUCCESS;
}

// Records AND submits per-frame with current MVP (use this in your main loop).
// Returns false when the swapchain is out of date (trigger your recreate path).
bool drawFrameWithMVP(VulkanContext& ctx, const float* mvp, DrawSceneFn drawScene) {
    // wait & reset fence for this frame
    if (vkWaitForFences(ctx.device, 1, &ctx.inFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;
    if (vkResetFences(ctx.device, 1, &ctx.inFlightFence) != VK_SUCCESS) return false;

    // acquire image
    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(ctx.device, ctx.swapchain, UINT64_MAX,
        ctx.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) return false;

    // record command buffer for this image
    VkCommandBuffer cb = ctx.commandBuffers[imageIndex];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return false;

    VkClearValue clears[2]{};
    clears[0].color = {{0.05f, 0.10f, 0.15f, 1.0f}};
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = ctx.renderPass;
    rp.framebuffer = ctx.framebuffers[imageIndex];
    rp.renderArea.offset = { 0, 0 };
    rp.renderArea.extent = ctx.swapchainExtent;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // pipeline -> descriptor set -> push constants (full 80B to BOTH stages)
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.voxelPipeline);

    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
        ctx.voxelPipelineLayout, 0, 1, &ctx.descSet, 0, nullptr);

    // 80 B push constants
    float pcData[20];
    memcpy(pcData, mvp, sizeof(float) * 16);
    pcData[16] = 1.0f / 4.0f;                // atlasScale.x
    pcData[17] = 1.0f / 4.0f;                // atlasScale.y
    pcData[18] = 1.0f / ctx.atlasWidth;      // atlasTexel.x  (add these two!)
    pcData[19] = 1.0f / ctx.atlasHeight;     // atlasTexel.y

    vkCmdPushConstants(cb, ctx.voxelPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(float) * 20, pcData);

    if (ctx.vertexBuffer && ctx.indexBuffer && ctx.indexCount > 0) {
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cb, 0, 1, &ctx.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cb, ctx.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, ctx.indexCount, 1, 0, 0, 0);
    }

    // >>> draw your scene (chunks) here <<<
    if (drawScene) drawScene(cb);

    vkCmdEndRenderPass(cb);
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) return false;

    // submit
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &ctx.imageAvailableSemaphore;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &ctx.renderFinishedSemaphore;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &submit, ctx.inFlightFence) != VK_SUCCESS) return false;

    // present
    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &ctx.renderFinishedSemaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &ctx.swapchain;
    present.pImageIndices = &imageIndex;

    VkResult pr = vkQueuePresentKHR(ctx.presentQueue, &present);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) return false;

    return pr == VK_SUCCESS;
}

void cleanupSwapchain(VulkanContext& ctx) {
    for (auto fb : ctx.framebuffers) if (fb) vkDestroyFramebuffer(ctx.device, fb, nullptr);
    ctx.framebuffers.clear();
    if (ctx.renderPass) vkDestroyRenderPass(ctx.device, ctx.renderPass, nullptr); ctx.renderPass = VK_NULL_HANDLE;
    for (auto iv : ctx.swapchainImageViews) if (iv) vkDestroyImageView(ctx.device, iv, nullptr);
    ctx.swapchainImageViews.clear();
    if (ctx.swapchain) vkDestroySwapchainKHR(ctx.device, ctx.swapchain, nullptr); ctx.swapchain = VK_NULL_HANDLE;
    destroyDepthResources(ctx);
    if (ctx.commandPool) {
        vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
        ctx.commandPool = VK_NULL_HANDLE;
        ctx.commandBuffers.clear();
    }
    if (ctx.imageAvailableSemaphore) { vkDestroySemaphore(ctx.device, ctx.imageAvailableSemaphore, nullptr); ctx.imageAvailableSemaphore = VK_NULL_HANDLE; }
    if (ctx.renderFinishedSemaphore) { vkDestroySemaphore(ctx.device, ctx.renderFinishedSemaphore, nullptr); ctx.renderFinishedSemaphore = VK_NULL_HANDLE; }
    if (ctx.inFlightFence) { vkDestroyFence(ctx.device, ctx.inFlightFence, nullptr); ctx.inFlightFence = VK_NULL_HANDLE; }
}


static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {
    std::cerr << "[VK] " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}

bool createInstance(VulkanContext& ctx, const char* appName, bool enableValidation) {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0,1,0,0);
    appInfo.pEngineName = "NoEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0,1,0,0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = nullptr; // will be filled by caller (GLFW) via vkGetInstanceProcAddr
    // We leave extensions to the caller (main) to set using glfwGetRequiredInstanceExtensions.
    // For simplicity, set to zero here; main will override via pNext trick isn't ideal.
    // Instead we rely on main to call vkCreateInstance with correct extensions.

    std::vector<const char*> layers;
    if (enableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    // The extensions must be set by the caller. We'll keep it empty here and let main fill.
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;

    // Note: we will not create the instance here; main will do it to pass extensions.
    // But to keep the interface, let's actually create it once main fills fields.
    // We'll return false to indicate main should create instance manually.
    return false;
}

void setupDebug(VulkanContext& ctx) {
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func) return;
    VkDebugUtilsMessengerCreateInfoEXT ci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    func(ctx.instance, &ci, nullptr, &ctx.debugMessenger);
}

void destroyDebug(VulkanContext& ctx) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func && ctx.debugMessenger) func(ctx.instance, ctx.debugMessenger, nullptr);
}

// Prefer a single family that supports BOTH graphics & present
static bool findQueueFamilies(VulkanContext& ctx, VkPhysicalDevice dev) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, props.data());

    // Pass 1: one family that can do both
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 canPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, ctx.surface, &canPresent);
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent) {
            ctx.graphicsQueueFamily = i;
            ctx.presentQueueFamily = i;
            return true;
        }
    }
    // Pass 2: any graphics + any present
    bool gFound = false, pFound = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!gFound && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            ctx.graphicsQueueFamily = i; gFound = true;
        }
        VkBool32 canPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, ctx.surface, &canPresent);
        if (!pFound && canPresent) {
            ctx.presentQueueFamily = i; pFound = true;
        }
    }
    return gFound && pFound;
}

bool pickPhysicalDevice(VulkanContext& ctx) {
    uint32_t n = 0; vkEnumeratePhysicalDevices(ctx.instance, &n, nullptr);
    if (n == 0) return false;
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(ctx.instance, &n, devs.data());
    for (auto d : devs) {
        ctx.physicalDevice = d;
        if (findQueueFamilies(ctx, d)) return true;
    }
    return false;
}

bool createDevice(VulkanContext& ctx) {
    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    std::vector<uint32_t> uniq = { ctx.graphicsQueueFamily };
    if (ctx.presentQueueFamily != ctx.graphicsQueueFamily)
        uniq.push_back(ctx.presentQueueFamily);

    for (uint32_t fam : uniq) {
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;
        qcis.push_back(qci);
    }

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(ctx.physicalDevice, &supported);
    ctx.anisotropyFeature = (supported.samplerAnisotropy == VK_TRUE);

    VkPhysicalDeviceFeatures feats{};
    const char* exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (supported.samplerAnisotropy) {
        feats.samplerAnisotropy = VK_TRUE;      // ask for it
    }

    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = (uint32_t)qcis.size();
    dci.pQueueCreateInfos = qcis.data();
    dci.pEnabledFeatures = &feats;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = exts;

    if (vkCreateDevice(ctx.physicalDevice, &dci, nullptr, &ctx.device) != VK_SUCCESS) return false;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    ctx.maxSamplerAnisotropy = props.limits.maxSamplerAnisotropy;

    vkGetDeviceQueue(ctx.device, ctx.graphicsQueueFamily, 0, &ctx.graphicsQueue);
    vkGetDeviceQueue(ctx.device, ctx.presentQueueFamily, 0, &ctx.presentQueue);

    if (ctx.graphicsQueueFamily == ctx.presentQueueFamily)
        ctx.presentQueue = ctx.graphicsQueue;

    return ctx.graphicsQueue && ctx.presentQueue;
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open file: " + path);
    size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    return module;
}

bool createPipeline(VulkanContext& ctx, const std::string& shaderDir) {
    // Load SPIR-V
    auto vert = readFile(shaderDir + "/triangle.vert.spv");
    auto frag = readFile(shaderDir + "/triangle.frag.spv");

    VkShaderModule vertModule = createShaderModule(ctx.device, vert);
    VkShaderModule fragModule = createShaderModule(ctx.device, frag);

    VkPipelineShaderStageCreateInfo vs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vs.module = vertModule;
    vs.pName = "main";

    VkPipelineShaderStageCreateInfo fs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fs.module = fragModule;
    fs.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    // No vertex buffers
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width = (float)ctx.swapchainExtent.width;
    vp.height = (float)ctx.swapchainExtent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = { 0,0 };
    sc.extent = ctx.swapchainExtent;

    VkPipelineViewportStateCreateInfo vpstate{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vpstate.viewportCount = 1; vpstate.pViewports = &vp;
    vpstate.scissorCount = 1; vpstate.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &ctx.pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(ctx.device, fragModule, nullptr);
        vkDestroyShaderModule(ctx.device, vertModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vpstate;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.layout = ctx.pipelineLayout;
    pci.renderPass = ctx.renderPass;
    pci.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &ctx.pipeline);

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return r == VK_SUCCESS;
}

void destroyPipeline(VulkanContext& ctx) {
    if (ctx.pipeline) { vkDestroyPipeline(ctx.device, ctx.pipeline, nullptr); ctx.pipeline = VK_NULL_HANDLE; }
    if (ctx.pipelineLayout) { vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, nullptr); ctx.pipelineLayout = VK_NULL_HANDLE; }
}

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("No suitable memory type");
}

bool createBuffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(ctx.device, &bi, nullptr, &buf) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, buf, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(ctx.physicalDevice, req.memoryTypeBits, props);
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(ctx.device, buf, mem, 0);
    return true;
}

static VkCommandBuffer beginOneShot(VulkanContext& ctx) {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(ctx.device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void endOneShot(VulkanContext& ctx, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
}

bool copyBuffer(VulkanContext& ctx, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBuffer cmd = beginOneShot(ctx);
    VkBufferCopy copy{ 0,0,size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &copy);
    endOneShot(ctx, cmd);
    return true;
}

bool uploadVoxelMesh(VulkanContext& ctx, const std::vector<float>& verts,
    const std::vector<uint32_t>& indices) {
    ctx.indexCount = static_cast<uint32_t>(indices.size());
    if (ctx.indexCount == 0) return true;

    VkDeviceSize vbytes = sizeof(float) * verts.size();
    VkDeviceSize ibytes = sizeof(uint32_t) * indices.size();

    // staging buffers
    VkBuffer vstage{}, istage{};
    VkDeviceMemory vstageMem{}, istageMem{};
    if (!createBuffer(ctx, vbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vstage, vstageMem)) return false;
    if (!createBuffer(ctx, ibytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        istage, istageMem)) return false;

    // map + copy
    void* p = nullptr;
    vkMapMemory(ctx.device, vstageMem, 0, vbytes, 0, &p);
    memcpy(p, verts.data(), (size_t)vbytes);
    vkUnmapMemory(ctx.device, vstageMem);

    vkMapMemory(ctx.device, istageMem, 0, ibytes, 0, &p);
    memcpy(p, indices.data(), (size_t)ibytes);
    vkUnmapMemory(ctx.device, istageMem);

    // device-local buffers
    if (!createBuffer(ctx, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ctx.vertexBuffer, ctx.vertexMemory)) return false;
    if (!createBuffer(ctx, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ctx.indexBuffer, ctx.indexMemory)) return false;

    // copy
    copyBuffer(ctx, vstage, ctx.vertexBuffer, vbytes);
    copyBuffer(ctx, istage, ctx.indexBuffer, ibytes);

    // cleanup staging
    vkDestroyBuffer(ctx.device, vstage, nullptr);
    vkFreeMemory(ctx.device, vstageMem, nullptr);
    vkDestroyBuffer(ctx.device, istage, nullptr);
    vkFreeMemory(ctx.device, istageMem, nullptr);

    return true;
}

void destroyVoxelMesh(VulkanContext& ctx) {
    if (ctx.indexBuffer) { vkDestroyBuffer(ctx.device, ctx.indexBuffer, nullptr); ctx.indexBuffer = VK_NULL_HANDLE; }
    if (ctx.indexMemory) { vkFreeMemory(ctx.device, ctx.indexMemory, nullptr); ctx.indexMemory = VK_NULL_HANDLE; }
    if (ctx.vertexBuffer) { vkDestroyBuffer(ctx.device, ctx.vertexBuffer, nullptr); ctx.vertexBuffer = VK_NULL_HANDLE; }
    if (ctx.vertexMemory) { vkFreeMemory(ctx.device, ctx.vertexMemory, nullptr); ctx.vertexMemory = VK_NULL_HANDLE; }
    ctx.indexCount = 0;
}

bool createVoxelPipeline(VulkanContext& ctx, const std::string& shaderDir) {
    auto vert = readFile(shaderDir + "/voxel.vert.spv");
    auto frag = readFile(shaderDir + "/voxel.frag.spv");
    VkShaderModule vmod = createShaderModule(ctx.device, vert);
    VkShaderModule fmod = createShaderModule(ctx.device, frag);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(float) * 20; // 80 bytes

    VkPipelineShaderStageCreateInfo vs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vs.stage = VK_SHADER_STAGE_VERTEX_BIT;   vs.module = vmod; vs.pName = "main";
    VkPipelineShaderStageCreateInfo fs{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fs.module = fmod; fs.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = { vs, fs };

    // binding 0: pos3 normal3 uv2 tile2  => 10 floats
    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = sizeof(float) * 10; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    // location 0: pos
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    // location 1: normal
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 };
    // location 2: uv
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,     sizeof(float) * 6 };
    // location 3: tile offset (tx/N, ty/N)
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,     sizeof(float) * 8 };

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport + scissor (static)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(ctx.swapchainExtent.width);
    viewport.height = static_cast<float>(ctx.swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = ctx.swapchainExtent;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.pViewports = &viewport;  // <- pointer to VkViewport
    vp.scissorCount = 1;
    vp.pScissors = &scissor;   // <- pointer to VkRect2D

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // because P[1][1] *= -1
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &ctx.descSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &ctx.voxelPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(ctx.device, fmod, nullptr);
        vkDestroyShaderModule(ctx.device, vmod, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;     // <-- ? REQUIRED when subpass has depth
    pci.pColorBlendState = &cb;
    pci.layout = ctx.voxelPipelineLayout;
    pci.renderPass = ctx.renderPass;
    pci.subpass = 0;

    VkResult r = vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &ctx.voxelPipeline);

    vkDestroyShaderModule(ctx.device, fmod, nullptr);
    vkDestroyShaderModule(ctx.device, vmod, nullptr);
    return r == VK_SUCCESS;
}

void destroyVoxelPipeline(VulkanContext& ctx) {
    if (ctx.voxelPipeline) { vkDestroyPipeline(ctx.device, ctx.voxelPipeline, nullptr); ctx.voxelPipeline = VK_NULL_HANDLE; }
    if (ctx.voxelPipelineLayout) { vkDestroyPipelineLayout(ctx.device, ctx.voxelPipelineLayout, nullptr); ctx.voxelPipelineLayout = VK_NULL_HANDLE; }
}

bool createImage(VulkanContext& ctx, uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags props,
    uint32_t mipLevels, VkImage& image, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = { w, h, 1 };
    ci.mipLevels = mipLevels;
    ci.arrayLayers = 1;
    ci.format = fmt;
    ci.tiling = tiling;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = usage;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(ctx.device, &ci, nullptr, &image) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx.device, image, &req);
    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(ctx.physicalDevice, req.memoryTypeBits, props);
    if (vkAllocateMemory(ctx.device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(ctx.device, image, mem, 0);
    return true;
}

static VkCommandBuffer beginOneShotImg(VulkanContext& ctx) {
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = ctx.commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(ctx.device, &ai, &cmd);
    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}
static void endOneShotImg(VulkanContext& ctx, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
}

bool transitionImageLayoutRange(VulkanContext& ctx, VkImage image, VkFormat fmt,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    uint32_t baseMip, uint32_t levelCount) {
    VkCommandBuffer cmd = beginOneShotImg(ctx);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask =
        (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        ? (hasStencilComponent(fmt) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            : VK_IMAGE_ASPECT_DEPTH_BIT)
        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // conservative src/dst stages:
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endOneShotImg(ctx, cmd);
    return true;
}

bool copyBufferToImage(VulkanContext& ctx, VkBuffer src, VkImage dst, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = beginOneShotImg(ctx);
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0; // tightly packed
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = { 0,0,0 };
    copy.imageExtent = { w,h,1 };
    vkCmdCopyBufferToImage(cmd, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    endOneShotImg(ctx, cmd);
    return true;
}

bool createTextureAtlasFromFile(VulkanContext& ctx, const char* path) {
    int w, h, c;
    stbi_uc* pixels = stbi_load(path, &w, &h, &c, STBI_rgb_alpha);
    if (!pixels) { std::cerr << "Failed to load image: " << path << "\n"; return false; }

    ctx.atlasWidth = static_cast<uint32_t>(w);
    ctx.atlasHeight = static_cast<uint32_t>(h);

    // mip count
    uint32_t mipLevels = 1u + (uint32_t)std::floor(std::log2((double)std::max(w, h)));

    // staging buffer
    VkDeviceSize size = (VkDeviceSize)w * h * 4;
    VkBuffer staging{};
    VkDeviceMemory stagingMem{};
    if (!createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingMem)) {
        stbi_image_free(pixels); return false;
    }
    void* p = nullptr;
    vkMapMemory(ctx.device, stagingMem, 0, size, 0, &p);
    memcpy(p, pixels, (size_t)size);
    vkUnmapMemory(ctx.device, stagingMem);
    stbi_image_free(pixels);

    // device image: needs SRC (for blits), DST, and SAMPLED
    if (!createImage(ctx, (uint32_t)w, (uint32_t)h, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        mipLevels, ctx.atlasImage, ctx.atlasMemory)) return false;

    // copy base level
    transitionImageLayoutRange(ctx, ctx.atlasImage, VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, mipLevels);

    if (!copyBufferToImage(ctx, staging, ctx.atlasImage, (uint32_t)w, (uint32_t)h)) return false;

    // check linear blit support (for mip gen)
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice, VK_FORMAT_R8G8B8A8_SRGB, &props);
    bool canLinearBlit = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

    // generate mips (or fall back)
    VkCommandBuffer cmd = beginOneShotImg(ctx);
    if (canLinearBlit && mipLevels > 1) {
        int32_t mipW = w, mipH = h;
        for (uint32_t i = 1; i < mipLevels; ++i) {
            // barrier: level i-1 to SRC
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = ctx.atlasImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            // blit i-1 -> i
            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipW, mipH, 1 };

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1 };

            // Transition dst level i to DST_OPTIMAL first
            VkImageMemoryBarrier dstBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            dstBarrier.srcAccessMask = 0;
            dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; // no-op; ensures proper subresourceRange
            dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstBarrier.image = ctx.atlasImage;
            dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            dstBarrier.subresourceRange.baseMipLevel = i;
            dstBarrier.subresourceRange.levelCount = 1;
            dstBarrier.subresourceRange.baseArrayLayer = 0;
            dstBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

            vkCmdBlitImage(cmd,
                ctx.atlasImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                ctx.atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            // after blit, old level (i-1) to SHADER_READ_ONLY_OPTIMAL
            VkImageMemoryBarrier toShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toShader.image = ctx.atlasImage;
            toShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            toShader.subresourceRange.baseMipLevel = i - 1;
            toShader.subresourceRange.levelCount = 1;
            toShader.subresourceRange.baseArrayLayer = 0;
            toShader.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toShader);

            mipW = std::max(mipW / 2, 1);
            mipH = std::max(mipH / 2, 1);
        }

        // transition last level to SHADER_READ_ONLY_OPTIMAL
        VkImageMemoryBarrier last{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        last.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        last.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        last.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        last.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        last.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        last.image = ctx.atlasImage;
        last.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        last.subresourceRange.baseMipLevel = mipLevels - 1;
        last.subresourceRange.levelCount = 1;
        last.subresourceRange.baseArrayLayer = 0;
        last.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &last);

    }
    else {
        // No linear blit support: just transition all to SHADER_READ_ONLY
        VkImageMemoryBarrier all{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        all.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        all.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        all.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        all.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        all.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        all.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        all.image = ctx.atlasImage;
        all.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        all.subresourceRange.baseMipLevel = 0;
        all.subresourceRange.levelCount = mipLevels;
        all.subresourceRange.baseArrayLayer = 0;
        all.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &all);
    }

    endOneShotImg(ctx, cmd);

    // cleanup staging
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);

    // view uses full mip chain
    VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image = ctx.atlasImage;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_R8G8B8A8_SRGB;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.baseMipLevel = 0;
    iv.subresourceRange.levelCount = mipLevels;
    iv.subresourceRange.baseArrayLayer = 0;
    iv.subresourceRange.layerCount = 1;
    if (vkCreateImageView(ctx.device, &iv, nullptr, &ctx.atlasView) != VK_SUCCESS) return false;

    // sampler: trilinear + (optional) anisotropy
    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod = 0.0f;
    const float tilePx = float(std::min(w, h) / c);
    si.maxLod = std::max(0.0f, std::floor(std::log2(tilePx)));
    si.mipLodBias = 0.0f;

    // enable anisotropy if device supports it
    VkPhysicalDeviceFeatures sup{};
    vkGetPhysicalDeviceFeatures(ctx.physicalDevice, &sup);
    if (sup.samplerAnisotropy) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = std::min(16.0f, ctx.maxSamplerAnisotropy); // 8x is plenty; clamp to device limit
    }
    else {
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy = 1.0f;
    }

    if (vkCreateSampler(ctx.device, &si, nullptr, &ctx.atlasSampler) != VK_SUCCESS) return false;

    return true;
}

static uint32_t findMemoryTypeOrInvalid(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
};

bool createMaterialUBO(VulkanContext& ctx) {
    auto mats = buildDefaultMaterials();
    size_t count = mats.size();
    size_t elem = sizeof(Material);
    size_t total = count * elem;

    if (count == 0 || total == 0) {
        fprintf(stderr, "[MatUBO] ERROR: empty material table (count=%zu, total=%zu).\n", count, total);
        // Make a minimal one so descriptors are valid
        mats.resize(1);
        total = sizeof(Material);
    }

    // Align to device limits (good hygiene for UBOs)
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice, &props);
    VkDeviceSize align = std::max<VkDeviceSize>(16, props.limits.minUniformBufferOffsetAlignment);
    VkDeviceSize sizeAligned = (VkDeviceSize)((total + align - 1) & ~(align - 1));

    fprintf(stderr, "[MatUBO] count=%zu elem=%zuB total=%zuB aligned=%lluB\n",
        count, elem, total, (unsigned long long)sizeAligned);

    // Create buffer
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = sizeAligned;
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult vr = vkCreateBuffer(ctx.device, &bi, nullptr, &ctx.materialUBO);
    if (vr) { fprintf(stderr, "[MatUBO] vkCreateBuffer failed %d\n", vr); return false; }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx.device, ctx.materialUBO, &req);

    uint32_t typeIndex = findMemoryTypeOrInvalid(
        ctx.physicalDevice, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (typeIndex == UINT32_MAX) {
        fprintf(stderr, "[MatUBO] No HOST_VISIBLE|COHERENT memory type. Will try DEVICE_LOCAL + staging.\n");
    }

    auto tryAlloc = [&](uint32_t idx, VkDeviceMemory& mem)->VkResult {
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;    // driver-required size
        ai.memoryTypeIndex = idx;
        return vkAllocateMemory(ctx.device, &ai, nullptr, &mem);
        };

    // Path A: Host-visible
    VkResult ar = VK_ERROR_MEMORY_MAP_FAILED; // init to non-success
    if (typeIndex != UINT32_MAX) {
        ar = tryAlloc(typeIndex, ctx.materialUBOMem);
        if (ar == VK_SUCCESS) {
            VK_CHECK(vkBindBufferMemory(ctx.device, ctx.materialUBO, ctx.materialUBOMem, 0));

            void* p = nullptr;
            VK_CHECK(vkMapMemory(ctx.device, ctx.materialUBOMem, 0, sizeAligned, 0, &p));
            std::memcpy(p, mats.data(), total);
            vkUnmapMemory(ctx.device, ctx.materialUBOMem);

            ctx.materialUBOSize = sizeAligned;
            return true;
        }
        fprintf(stderr, "[MatUBO] Host-visible vkAllocateMemory failed: %d (req.size=%llu)\n",
            ar, (unsigned long long)req.size);
    }

    // Path B: Fallback to DEVICE_LOCAL + staging
    // Create a temporary staging buffer to upload once
    uint32_t stagingIndex = findMemoryTypeOrInvalid(
        ctx.physicalDevice, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Recreate device-local buffer
    vkDestroyBuffer(ctx.device, ctx.materialUBO, nullptr);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK_CHECK(vkCreateBuffer(ctx.device, &bi, nullptr, &ctx.materialUBO));
    vkGetBufferMemoryRequirements(ctx.device, ctx.materialUBO, &req);

    // pick a DEVICE_LOCAL type
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &mp);
    uint32_t devLocalIdx = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            devLocalIdx = i; break;
        }
    }
    if (devLocalIdx == UINT32_MAX) {
        fprintf(stderr, "[MatUBO] No DEVICE_LOCAL memory type. Aborting.\n");
        return false;
    }

    ar = tryAlloc(devLocalIdx, ctx.materialUBOMem);
    if (ar) {
        fprintf(stderr, "[MatUBO] DEVICE_LOCAL vkAllocateMemory failed: %d (req.size=%llu)\n",
            ar, (unsigned long long)req.size);
        return false;
    }
    VK_CHECK(vkBindBufferMemory(ctx.device, ctx.materialUBO, ctx.materialUBOMem, 0));

    // staging buffer
    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo sbi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    sbi.size = sizeAligned;
    sbi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(ctx.device, &sbi, nullptr, &staging));
    VkMemoryRequirements sreq{}; vkGetBufferMemoryRequirements(ctx.device, staging, &sreq);
    uint32_t stagIdx = findMemoryTypeOrInvalid(
        ctx.physicalDevice, sreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagIdx == UINT32_MAX || tryAlloc(stagIdx, stagingMem)) {
        fprintf(stderr, "[MatUBO] staging alloc failed.\n");
        return false;
    }
    VK_CHECK(vkBindBufferMemory(ctx.device, staging, stagingMem, 0));
    void* sp = nullptr; VK_CHECK(vkMapMemory(ctx.device, stagingMem, 0, sizeAligned, 0, &sp));
    std::memcpy(sp, mats.data(), total);
    vkUnmapMemory(ctx.device, stagingMem);

    // copy command
    VkCommandBufferAllocateInfo cbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbi.commandPool = ctx.commandPool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    VkCommandBuffer cmd; VK_CHECK(vkAllocateCommandBuffers(ctx.device, &cbi, &cmd));
    VkCommandBufferBeginInfo bi2{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi2.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi2));
    VkBufferCopy region{ 0,0,sizeAligned };
    vkCmdCopyBuffer(cmd, staging, ctx.materialUBO, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(ctx.graphicsQueue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(ctx.graphicsQueue));
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);

    ctx.materialUBOSize = sizeAligned;
    return true;
}

// Descriptors: set 0, binding 0 = combined image sampler
bool createDescriptors(VulkanContext& ctx) {
    // 0) Must have resources ready
    if (ctx.atlasSampler == VK_NULL_HANDLE || ctx.atlasView == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[VK][Descriptors] atlas sampler/view not ready.\n");
        return false;
    }
    if (ctx.materialUBO == VK_NULL_HANDLE || ctx.materialUBOSize == 0) {
        std::fprintf(stderr, "[VK][Descriptors] materialUBO not created.\n");
        return false;
    }

    // 1) Pool
    if (ctx.descPool) {
        vkDestroyDescriptorPool(ctx.device, ctx.descPool, nullptr);
        ctx.descPool = VK_NULL_HANDLE;
    }

    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[0].descriptorCount = 4;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         sizes[1].descriptorCount = 4;

    VkDescriptorPoolCreateInfo dp{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dp.flags = 0;                // don’t set FREE_DESCRIPTOR_SET_BIT unless you really free sets
    dp.maxSets = 4;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = sizes;

    if (vkCreateDescriptorPool(ctx.device, &dp, nullptr, &ctx.descPool) != VK_SUCCESS) {
        std::fprintf(stderr, "Fatal: Create Descriptor pool failed\n");
        return false;
    }

    // 2) Layout (set = 0, binding0: sampler, binding1: UBO)
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0; b0.descriptorCount = 1;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding b1{};
    b1.binding = 1; b1.descriptorCount = 1;
    b1.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[2] = { b0, b1 };
    VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    lci.bindingCount = 2;
    lci.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(ctx.device, &lci, nullptr, &ctx.descSetLayout) != VK_SUCCESS) {
        std::fprintf(stderr, "Fatal: Create descriptor set layout failed\n");
        return false;
    }

    // 3) Allocate
    VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = ctx.descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &ctx.descSetLayout;

    if (vkAllocateDescriptorSets(ctx.device, &ai, &ctx.descSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Fatal: allocate descriptor set failed\n");
        return false;
    }

    // 4) Update set — REQUIRE valid ctx.materialUBO here
    if (ctx.materialUBO == VK_NULL_HANDLE) {
        fprintf(stderr, "[VK][Descriptors] materialUBO is NULL — createMaterialUBO must run before createDescriptors.\n");
        return false;
    }

    VkDescriptorImageInfo ii{};
    ii.sampler = ctx.atlasSampler;
    ii.imageView = ctx.atlasView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo ubo{};
    ubo.buffer = ctx.materialUBO;
    ubo.offset = 0;
    ubo.range = ctx.materialUBOSize ? ctx.materialUBOSize : sizeof(Material);

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ctx.descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &ii;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ctx.descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &ubo;

    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
    return true;
}

void destroyDescriptors(VulkanContext& ctx) {
    if (ctx.descPool) { vkDestroyDescriptorPool(ctx.device, ctx.descPool, nullptr); ctx.descPool = VK_NULL_HANDLE; }
    if (ctx.descSetLayout) { vkDestroyDescriptorSetLayout(ctx.device, ctx.descSetLayout, nullptr); ctx.descSetLayout = VK_NULL_HANDLE; }
    if (ctx.atlasSampler) { vkDestroySampler(ctx.device, ctx.atlasSampler, nullptr); ctx.atlasSampler = VK_NULL_HANDLE; }
    if (ctx.atlasView) { vkDestroyImageView(ctx.device, ctx.atlasView, nullptr); ctx.atlasView = VK_NULL_HANDLE; }
    if (ctx.atlasImage) { vkDestroyImage(ctx.device, ctx.atlasImage, nullptr); ctx.atlasImage = VK_NULL_HANDLE; }
    if (ctx.atlasMemory) { vkFreeMemory(ctx.device, ctx.atlasMemory, nullptr); ctx.atlasMemory = VK_NULL_HANDLE; }
}

VkFormat findSupportedFormat(VkPhysicalDevice phys,
    const std::vector<VkFormat>& candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(phys, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("No supported image format found");
}

VkFormat findDepthFormat(VulkanContext& ctx) {
    // Prefer D32_SFLOAT; fall back to D24_UNORM_S8_UINT, D32_SFLOAT_S8_UINT if needed
    ctx.depthFormat = findSupportedFormat(
        ctx.physicalDevice,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
    return ctx.depthFormat;
}

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool createDepthResources(VulkanContext& ctx, uint32_t width, uint32_t height) {
    // Pick a supported depth format (also stores ctx.depthFormat)
    VkFormat fmt = findDepthFormat(ctx);

    // Create depth image (device-local)
    if (!createImage(ctx, width, height, fmt,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        /*mipLevels*/ 1,
        ctx.depthImage, ctx.depthMemory)) {
        std::cerr << "[VK] Depth: createImage failed\n";
        return false;
    }

    // Create view (no layout transition needed here)
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencilComponent(fmt)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image = ctx.depthImage;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = fmt;
    iv.subresourceRange.aspectMask = aspect;
    iv.subresourceRange.baseMipLevel = 0;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.baseArrayLayer = 0;
    iv.subresourceRange.layerCount = 1;

    VkResult r = vkCreateImageView(ctx.device, &iv, nullptr, &ctx.depthView);
    if (r != VK_SUCCESS || !ctx.depthView) {
        std::cerr << "[VK] Depth: vkCreateImageView failed, r=" << (int)r << "\n";
        return false;
    }
}

void destroyDepthResources(VulkanContext& ctx) {
    if (ctx.depthView) { vkDestroyImageView(ctx.device, ctx.depthView, nullptr); ctx.depthView = VK_NULL_HANDLE; }
    if (ctx.depthImage) { vkDestroyImage(ctx.device, ctx.depthImage, nullptr);    ctx.depthImage = VK_NULL_HANDLE; }
    if (ctx.depthMemory) { vkFreeMemory(ctx.device, ctx.depthMemory, nullptr);     ctx.depthMemory = VK_NULL_HANDLE; }
}

bool recreateAtlasSampler(VulkanContext& ctx, float anisoLevel) {
    // clamp + feature guard
        if (!ctx.anisotropyFeature) anisoLevel = 1.0f;
    anisoLevel = std::max(1.0f, std::min(anisoLevel, ctx.maxSamplerAnisotropy));

    // build the new sampler first
    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod = 0.0f;
    si.maxLod = 1000.0f; // view clamps actual mip count
    si.mipLodBias = 0.0f;

    if (ctx.anisotropyFeature && anisoLevel > 1.0f) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = anisoLevel;
    }
    else {
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy = 1.0f;
    }

    VkSampler newSampler = VK_NULL_HANDLE;
    if (vkCreateSampler(ctx.device, &si, nullptr, &newSampler) != VK_SUCCESS) {
        std::cerr << "[VK] create new sampler failed\n";
        return false;
    }

    // Make sure no in-flight work still reads the old sampler
    // (AF toggle is rare; a short stall is fine.)
    vkDeviceWaitIdle(ctx.device);

    // Re-point the descriptor set to the new sampler
    VkDescriptorImageInfo ii{};
    ii.sampler = newSampler;
    ii.imageView = ctx.atlasView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = ctx.descSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &ii;

    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

    // Destroy the old sampler (now it’s guaranteed not in use)
    if (ctx.atlasSampler) vkDestroySampler(ctx.device, ctx.atlasSampler, nullptr);

    ctx.atlasSampler = newSampler;
    ctx.currentAniso = anisoLevel;
    return true;
}

static bool createAndFillDeviceLocalBuffer(VulkanContext& ctx,
    const void* srcData, VkDeviceSize srcBytes,
    VkBufferUsageFlags usage,
    VkBuffer& outBuf, VkDeviceMemory& outMem)
{
    if (srcBytes == 0) { outBuf = VK_NULL_HANDLE; outMem = VK_NULL_HANDLE; return true; }

    // 1) staging (HOST_VISIBLE, SRC)
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!createBuffer(ctx, srcBytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging, stagingMem))
        return false;

    void* mapped = nullptr;
    vkMapMemory(ctx.device, stagingMem, 0, srcBytes, 0, &mapped);
    std::memcpy(mapped, srcData, (size_t)srcBytes);
    vkUnmapMemory(ctx.device, stagingMem);

    // 2) device local dst (DST | usage)
    if (!createBuffer(ctx, srcBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outBuf, outMem))
    {
        vkDestroyBuffer(ctx.device, staging, nullptr);
        vkFreeMemory(ctx.device, stagingMem, nullptr);
        return false;
    }

    // 3) copy
    if (!copyBuffer(ctx, staging, outBuf, srcBytes)) {
        vkDestroyBuffer(ctx.device, outBuf, nullptr);
        vkFreeMemory(ctx.device, outMem, nullptr);
        vkDestroyBuffer(ctx.device, staging, nullptr);
        vkFreeMemory(ctx.device, stagingMem, nullptr);
        return false;
    }

    // 4) cleanup staging
    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);
    return true;
}

bool uploadRegionMesh(VulkanContext& ctx,
    RegionGPU& dst,
    const std::vector<float>& vertices,
    const std::vector<uint32_t>& indices)
{
    // If GPU might still be using old buffers, either:
    //  - call this after your frame fence has signaled, or
    //  - use a small garbage list & destroy later. For simplicity here, we just destroy now.
    destroyRegionBuffers(ctx, dst);

    // Create & fill VBO
    if (!vertices.empty()) {
        if (!createAndFillDeviceLocalBuffer(ctx, vertices.data(),
            (VkDeviceSize)vertices.size() * sizeof(float),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, dst.vbo, dst.vmem))
        {
            std::cerr << "[VK] uploadRegionMesh: VBO failed\n";
            destroyRegionBuffers(ctx, dst);
            return false;
        }
    }

    // Create & fill IBO
    if (!indices.empty()) {
        if (!createAndFillDeviceLocalBuffer(ctx, indices.data(),
            (VkDeviceSize)indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, dst.ibo, dst.imem))
        {
            std::cerr << "[VK] uploadRegionMesh: IBO failed\n";
            destroyRegionBuffers(ctx, dst);
            return false;
        }
    }

    dst.indexCount = (uint32_t)indices.size();
    return true;
}

void destroyRegionBuffers(VulkanContext& ctx, RegionGPU& rgn)
{
    if (rgn.vbo) { vkDestroyBuffer(ctx.device, rgn.vbo, nullptr); rgn.vbo = VK_NULL_HANDLE; }
    if (rgn.vmem) { vkFreeMemory(ctx.device, rgn.vmem, nullptr); rgn.vmem = VK_NULL_HANDLE; }
    if (rgn.ibo) { vkDestroyBuffer(ctx.device, rgn.ibo, nullptr); rgn.ibo = VK_NULL_HANDLE; }
    if (rgn.imem) { vkFreeMemory(ctx.device, rgn.imem, nullptr); rgn.imem = VK_NULL_HANDLE; }
    rgn.indexCount = 0;
}

void destroyMaterialUBO(VulkanContext& ctx) {
    if (ctx.materialUBOMem) { vkFreeMemory(ctx.device, ctx.materialUBOMem, nullptr); ctx.materialUBOMem = VK_NULL_HANDLE; }
    if (ctx.materialUBO) { vkDestroyBuffer(ctx.device, ctx.materialUBO, nullptr); ctx.materialUBO = VK_NULL_HANDLE; }
    ctx.materialUBOSize = 0;
}