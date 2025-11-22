#include "debug_tools.hpp"
#include <iostream>
#include <cstring>

#include "settings.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

static VkDescriptorPool g_imguiPool = VK_NULL_HANDLE;

// ===================== world stats =====================
void dbgCollectWorldStats(const World& w, DebugStats& s) {
    s.chunksTotal = (uint32_t)w.map.size();
    s.chunksReady = 0;
    s.tris = 0;
    for (auto& kv : w.map) {
        const auto& wc = *kv.second;
        s.tris += wc.meshCPU.indices.size() / 3;
        const auto& g = wc.gpu;
        if (g.vbo && g.ibo && g.indexCount > 0) s.chunksReady++;
    }
}
void dbgSetCamera(DebugStats& s, const glm::vec3& pos, float yaw, float pitch) {
    s.camPos = pos; s.camYaw = yaw; s.camPitch = pitch;
}
void dbgSetFrame(DebugStats& s, float dt) {
    s.dt = dt; if (dt > 0.f) s.fps = 1.0f / dt;
}

void dbgLogOnceBoot(const World& w) {
    size_t tris = 0;
    for (auto& kv : w.map) tris += kv.second->meshCPU.indices.size() / 3;
    std::cerr << "[BOOT] chunks=" << w.map.size() << " tris=" << tris << "\n";
}

// ===================== debug utils (labels + messenger) =====================
static PFN_vkCmdBeginDebugUtilsLabelEXT pfnBeginLabel = nullptr;
static PFN_vkCmdEndDebugUtilsLabelEXT   pfnEndLabel = nullptr;

static VKAPI_ATTR VkBool32 VKAPI_CALL dbgCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/)
{
    const char* sev =
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "WARN " :
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "INFO " : "VERB ";
    std::cerr << "[VK][" << sev << "] " << (callbackData->pMessage ? callbackData->pMessage : "(null)") << "\n";
    return VK_FALSE;
}

bool dbgInitVkDebugUtils(VulkanContext& ctx) {
    // load function pointers for labels
    pfnBeginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(ctx.instance, "vkCmdBeginDebugUtilsLabelEXT");
    pfnEndLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(ctx.instance, "vkCmdEndDebugUtilsLabelEXT");

    // setup messenger if you created instance with VK_EXT_debug_utils
    auto vkCreateDebugUtilsMessengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT) return true; // not fatal

    VkDebugUtilsMessengerCreateInfoEXT ci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = dbgCallback;

    VkDebugUtilsMessengerEXT msgr = VK_NULL_HANDLE;
    if (vkCreateDebugUtilsMessengerEXT(ctx.instance, &ci, nullptr, &msgr) != VK_SUCCESS) {
        std::cerr << "[VK] Debug messenger creation failed (ok to continue).\n";
    }
    else {
        // store if you want to destroy later; or leak on shutdown (debug-only)
    }
    return true;
}

void dbgBeginLabel(VulkanContext& /*ctx*/, VkCommandBuffer cb, const char* name) {
    if (!pfnBeginLabel) return;
    VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    l.pLabelName = name;
    float c[4] = { 0.2f, 0.6f, 1.0f, 1.0f };
    std::memcpy(l.color, c, sizeof(c));
    pfnBeginLabel(cb, &l);
}
void dbgEndLabel(VulkanContext& /*ctx*/, VkCommandBuffer cb) {
    if (!pfnEndLabel) return;
    pfnEndLabel(cb);
}

// ===================== Dear ImGui overlay (optional) =====================
/* If you already have ImGui elsewhere, keep only dbgImGuiDraw and call your own init.
   Minimal hooks assuming backends: imgui_impl_glfw.h / imgui_impl_vulkan.h
   If you don't have ImGui yet, you can skip init/draw; stats will still log to console.
*/
#include <imgui.h>
#define IMGUI_IMPL_VULKAN_H_
#ifdef IMGUI_IMPL_VULKAN_H_
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#endif

bool dbgImGuiInit(VulkanContext& ctx, GLFWwindow* win) {
#ifndef IMGUI_VERSION
    (void)ctx; (void)win; return false;
#else
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(win, true);

    // Create a small dedicated descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                16 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          16 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   16 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         16 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 16 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       16 },
    };
    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 16 * (uint32_t)(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
    dpci.poolSizeCount = (uint32_t)(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
    dpci.pPoolSizes = pool_sizes;
    if (vkCreateDescriptorPool(ctx.device, &dpci, nullptr, &g_imguiPool) != VK_SUCCESS) {
        std::cerr << "[ImGui] Failed to create descriptor pool.\n";
        return false;
    }

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = ctx.instance;
    ii.PhysicalDevice = ctx.physicalDevice;
    ii.Device = ctx.device;
    ii.Queue = ctx.graphicsQueue;
    ii.DescriptorPool = g_imguiPool;    // <<< use our local pool
    ii.MinImageCount = (uint32_t)ctx.swapchainImages.size();
    ii.ImageCount = (uint32_t)ctx.swapchainImages.size();
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.Subpass = 0;

    // *** IMPORTANT: provide the render pass ***
    ii.RenderPass = ctx.renderPass;   // <-- add this line
    #if IMGUI_VERSION_NUM >= 19000  // newer backends accept (info, render_pass)
        ImGui_ImplVulkan_Init(&ii);
    #else                           // older backends accept only (info)
        ImGui_ImplVulkan_Init(&ii);
    #endif

    // (Optional) upload fonts here with a one-off command buffer/submit
    return true;
#endif
}


void dbgImGuiNewFrame() {
#ifdef IMGUI_VERSION
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplVulkan_NewFrame();   // RENDERER backend  <<< important
    ImGui::NewFrame();
#endif
}

void dbgImGuiDraw(VulkanContext& ctx, VkCommandBuffer cb, const DebugStats& s) {
#ifdef IMGUI_VERSION
    if (!s.overlay) return;

    ImGui::SetNextWindowBgAlpha(0.25f);
    ImGui::Begin("Debug", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    ImGui::Text("FPS: %.1f  (dt=%.3f)", s.fps, s.dt);
    ImGui::Separator();
    ImGui::Text("Cam:  x=%.2f  y=%.2f  z=%.2f", s.camPos.x, s.camPos.y, s.camPos.z);
    ImGui::Text("Yaw: %.1f  Pitch: %.1f", s.camYaw, s.camPitch);
    ImGui::Separator();
    ImGui::Text("Chunks: %u total  %u ready", s.chunksTotal, s.chunksReady);
    ImGui::Text("Tris:   %llu", (unsigned long long)s.tris);

    ImGui::Separator();
    ImGui::Text("Streaming");
    ImGui::SliderInt("View Distance (chunks)", &gViewDist, 1, 8);
    ImGui::SliderInt("Unload Slack", &gUnloadSlack, 0, 2);
    // Changing the slider will automatically trigger the block above next frame

    static char pathBuf[256] = "saves/world.vwld";
    static std::string lastMsg;

    ImGui::Separator();
    ImGui::Text("World I/O");
    ImGui::InputText("Path", pathBuf, IM_ARRAYSIZE(pathBuf));

    if (ImGui::Button("Save")) {
        std::string err;
        if (worldSaveToFile(*s.worldRef, pathBuf, &err))  // see below: pass world ptr
            lastMsg = std::string("Saved to ") + pathBuf;
        else
            lastMsg = std::string("Save failed: ") + err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string err;
        if (worldLoadFromFile(*s.worldRef, pathBuf, &err)) {
            // Upload all dirty chunks now
            worldUploadDirty(*s.worldRef, *s.ctxRef); // see below for passing ctx
            lastMsg = std::string("Loaded from ") + pathBuf;
        }
        else {
            lastMsg = std::string("Load failed: ") + err;
        }
    }
    if (!lastMsg.empty()) ImGui::TextWrapped("%s", lastMsg.c_str());

    // === Window / Display ===
    static int modeIndex = 0; // 0=Windowed, 1=Borderless, 2=Exclusive
    static const char* modes[] = { "Windowed", "Borderless Fullscreen", "Exclusive Fullscreen" };
    ImGui::Separator();
    ImGui::Text("Display");
    ImGui::Combo("Mode", &modeIndex, modes, IM_ARRAYSIZE(modes));

    // Common resolutions (you can expand this)
    struct Res { int w, h; const char* label; };
    static Res presets[] = {
        {1280, 720,  "1280 x 720"},
        {1600, 900,  "1600 x 900"},
        {1920, 1080, "1920 x 1080"},
        {2560, 1440, "2560 x 1440"},
        {3840, 2160, "3840 x 2160"},
    };
    static int presetIndex = 2; // default 1920x1080
    ImGui::Combo("Resolution", &presetIndex,
        [](void* data, int idx, const char** out_text) {
            auto* a = (Res*)data; *out_text = a[idx].label; return true;
        },
        presets, (int)(sizeof(presets) / sizeof(presets[0])));

    // Apply buttons
    if (ImGui::Button("Apply Display Settings")) {
        // Save/restore windowed size & pos (static across frames)
        static int prevX = 100, prevY = 100, prevW = 1280, prevH = 720;

        GLFWwindow* win = ctx.window;               // <- you already store this in VulkanContext
        GLFWmonitor* mon = glfwGetPrimaryMonitor(); // pick primary; add a monitor combo later if you like
        const GLFWvidmode* vm = glfwGetVideoMode(mon);

        int ww = presets[presetIndex].w;
        int wh = presets[presetIndex].h;
        ctx.framebufferResized = true;

        if (modeIndex == 0) { // Windowed
            // restore decorations & size/pos
            glfwSetWindowAttrib(win, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(win, nullptr, prevX, prevY, ww, wh, 0);
        }
        else if (modeIndex == 1) { // Borderless fullscreen (windowed)
            // remember current pos/size before going borderless
            glfwGetWindowPos(win, &prevX, &prevY);
            glfwGetWindowSize(win, &prevW, &prevH);

            glfwSetWindowAttrib(win, GLFW_DECORATED, GLFW_FALSE);
            // place over the monitor area, size = monitor mode
            glfwSetWindowMonitor(win, nullptr, 0, 0, vm->width, vm->height, 0);
        }
        else { // Exclusive fullscreen
            // remember current pos/size before switching
            glfwGetWindowPos(win, &prevX, &prevY);
            glfwGetWindowSize(win, &prevW, &prevH);

            // set exact video mode (refresh from vm)
            glfwSetWindowMonitor(win, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        }
    }


    ImGui::End();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);  // <-- use the cb you’re recording
#else
    (void)ctx; (void)cb; (void)s;
#endif
}

void dbgImGuiShutdown() {
#ifdef IMGUI_VERSION
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    if (g_imguiPool) { /* optional: keep across recreates */
        // vkDestroyDescriptorPool(ctx.device, g_imguiPool, nullptr); // if you want
        // g_imguiPool = VK_NULL_HANDLE;
    }
    ImGui::DestroyContext(nullptr);
#endif
}

bool dbgImGuiReinit(VulkanContext& ctx, GLFWwindow* win) {
#ifdef IMGUI_VERSION
    dbgImGuiShutdown();
    return dbgImGuiInit(ctx, win);
#else
    (void)ctx; (void)win; return false;
#endif
}