#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "vk_utils.hpp"
#include "world/world.hpp"
#include "world/world_io.hpp"
#include "GLFW/glfw3.h"

// --------- stats to show in overlay/log ----------
struct DebugStats {
    VulkanContext* ctxRef = nullptr;
    // frame
    float   fps = 0.0f;
    float   dt = 0.0f;

    // world
    World* worldRef = nullptr;     // read-only in UI; cast away const if you call io
    uint32_t chunksTotal = 0;
    uint32_t chunksReady = 0;   // have VBO+IBO+indices>0
    uint64_t tris = 0;

    // camera
    glm::vec3 camPos{ 0 };
    float     camYaw = 0.f, camPitch = 0.f;

    // misc
    bool overlay = true; // toggle with F3
};

void dbgCollectWorldStats(const World& w, DebugStats& s);
void dbgSetCamera(DebugStats& s, const glm::vec3& pos, float yaw, float pitch);
void dbgSetFrame(DebugStats& s, float dt);

// --------- vk debug utils (labels) ----------
bool dbgInitVkDebugUtils(VulkanContext& ctx); // setup messenger if available
void dbgBeginLabel(VulkanContext& ctx, VkCommandBuffer cb, const char* name);
void dbgEndLabel(VulkanContext& ctx, VkCommandBuffer cb);

struct ScopedGpuLabel {
    VulkanContext* c; VkCommandBuffer cb;
    ScopedGpuLabel(VulkanContext& ctx, VkCommandBuffer _cb, const char* name) { c = &ctx; cb = _cb; dbgBeginLabel(ctx, cb, name); }
    ~ScopedGpuLabel() { dbgEndLabel(*c, cb); }
};

// --------- overlay (Dear ImGui) ----------
bool dbgImGuiInit(VulkanContext& ctx, GLFWwindow* win);
void dbgImGuiNewFrame();
void dbgImGuiDraw(VulkanContext& ctx, VkCommandBuffer cb, const DebugStats& s);
void dbgImGuiShutdown();
bool dbgImGuiReinit(VulkanContext& ctx, GLFWwindow* win);  // calls shutdown+init

// fallback logging if no imgui:
void dbgLogOnceBoot(const World& w);