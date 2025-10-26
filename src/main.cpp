#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <optional>
#include <limits>
#include <chrono>
#include <memory>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

//#include "input.hpp"
#include "vk_utils.hpp"
#include "debug_tools.hpp"
#include "camera.hpp"
#include "audio.hpp"
#include "materials.hpp"
#include "world/chunk.hpp"
#include "world/mesher.hpp"
#include "world/world_edit.hpp"
#include "world/world_raycast.hpp"
#include "world/world.hpp"
#include "player.hpp"


float pickMaxDist = 8.0f;
bool physicsMode = true;
//static Input input;
static DebugStats debugStats;
EditMode editMode = EditMode::Small;
Player player;
World world;
static Audio gAudio;

std::vector<bool> regionDirty(REGION_COUNT, false);
// CPU mesh cache per region
std::vector<MeshData> regionCpu(REGION_COUNT);

static void markRegionForCell(std::vector<bool>& flags, int x, int y, int z)
{
    // which region contains this cell?
    int rx = clampi(x / REGION_SIZE, 0, REGIONS_X - 1);
    int ry = clampi(y / REGION_SIZE, 0, REGIONS_Y - 1);
    int rz = clampi(z / REGION_SIZE, 0, REGIONS_Z - 1);

    flags[regionIndex(rx, ry, rz)] = true;

    // also mark neighbor regions when we touch the border,
    // so faces across region edges get updated
    if (x % REGION_SIZE == 0 && rx > 0)               flags[regionIndex(rx - 1, ry, rz)] = true;
    if (x % REGION_SIZE == REGION_SIZE - 1 && rx < REGIONS_X - 1)  flags[regionIndex(rx + 1, ry, rz)] = true;

    if (y % REGION_SIZE == 0 && ry > 0)               flags[regionIndex(rx, ry - 1, rz)] = true;
    if (y % REGION_SIZE == REGION_SIZE - 1 && ry < REGIONS_Y - 1)  flags[regionIndex(rx, ry + 1, rz)] = true;
     
    if (z % REGION_SIZE == 0 && rz > 0)               flags[regionIndex(rx, ry, rz - 1)] = true;
    if (z % REGION_SIZE == REGION_SIZE - 1 && rz < REGIONS_Z - 1)  flags[regionIndex(rx, ry, rz + 1)] = true;
}

static void glfwErrorCallback(int code, const char* desc) 
{
    std::cerr << "[GLFW] (" << code << ") " << desc << std::endl;
}

// at top-scope of main.cpp
static double g_scrollY = 0.0;
static void scroll_cb(GLFWwindow*, double /*xoff*/, double yoff) { g_scrollY += yoff; }

int main() 
{
    try {
        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan not supported");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "Voxel Game (Starter)", nullptr, nullptr);
        if (!window) throw std::runtime_error("Failed to create window");

        //input.init(window);
        glfwSetScrollCallback(window, scroll_cb);

        world.seed = 1337u;

        VulkanContext ctx{};

        // Create instance with required extensions
        uint32_t extCount = 0;
        const char** exts = glfwGetRequiredInstanceExtensions(&extCount);
        std::vector<const char*> extensions(exts, exts + extCount);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Voxel Game";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0,1,0,0);
        appInfo.pEngineName = "NoEngine";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0,1,0,0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };

        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &appInfo;
        ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ici.ppEnabledExtensionNames = extensions.data();
#if defined(NDEBUG)
        ici.enabledLayerCount = 0;
        ici.ppEnabledLayerNames = nullptr;
#else
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = layers;
#endif
        if (vkCreateInstance(&ici, nullptr, &ctx.instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }

        // then your existing audio init
        gAudio.init();
        gAudio.loadEvent("block_destroy", "assets/sfx/destroy.wav");

        // Create surface
        if (glfwCreateWindowSurface(ctx.instance, window, nullptr, &ctx.surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }

        static int   afIndex = 0;
        static bool  fWasDown = false;                            // edge detector
        static bool  f3WasDown = false;
        int currentMaterial = 1;
        auto clampMat = [&](int m) {
            if (m < 1) m = MAX_MATERIALS - 1;                 // wrap
            if (m >= MAX_MATERIALS) m = 1;                    // wrap
            return m;
            };

        if (!pickPhysicalDevice(ctx)) throw std::runtime_error("No Vulkan device found");
        if (!createDevice(ctx)) throw std::runtime_error("Failed to create device");
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);

        if (!createSwapchain(ctx, (uint32_t)fbw, (uint32_t)fbh)) throw std::runtime_error("swapchain failed");
        if (!createImageViews(ctx)) throw std::runtime_error("image views failed");
        if (!createRenderPass(ctx)) throw std::runtime_error("render pass failed");
        if (!createDepthResources(ctx, ctx.swapchainExtent.width, ctx.swapchainExtent.height))
            throw std::runtime_error("depth resources failed");
        if (!dbgImGuiInit(ctx, window)) throw std::runtime_error("Debug failed");
        if (!createFramebuffers(ctx)) throw std::runtime_error("framebuffers failed");
        if (!createCommandPoolAndBuffers(ctx))
            throw std::runtime_error("cmd pool/buffers failed");
        setupDebug(ctx);
        FPSCamera cam;
        cam.setViewportSize(ctx.swapchainExtent.width, ctx.swapchainExtent.height);
        cam.setCursorCaptured(window, true);
        cam.position = { 8.0f, 8.0f, 30.0f }; // tweak as you like
        cam.yaw = -90.0f; cam.pitch = 0.0f;
        std::string shaderDir = std::string("shaders"); // runtime dir = ${build}/shaders
        // Create (or keep) texture + descriptors ONCE
        static bool descriptorsReady = false;
        if (!descriptorsReady) {
            if (!createTextureAtlasFromFile(ctx, "assets/atlas.png"))
                throw std::runtime_error("atlas load failed");
            descriptorsReady = true;
        }
        if (!createMaterialUBO(ctx)) throw std::runtime_error("material creation failed");
        if (!createDescriptors(ctx))                               // <— makes descSetLayout
            throw std::runtime_error("descriptors failed");
        if (!createVoxelPipeline(ctx, "shaders")) throw std::runtime_error("voxel pipeline failed");
        // after swapchain/framebuffers/cmd pool and BEFORE recordCommandBuffers:
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
            (float)ctx.swapchainExtent.width / (float)ctx.swapchainExtent.height,
            0.1f, 1000.0f);
        // flip Y for Vulkan NDC
        proj[1][1] *= -1.0f;

        glm::vec3 eye = { 8.0f, 8.0f, 30.0f };   // camera position
        glm::vec3 target = { 8.0f, 2.0f, 8.0f }; // look at chunk center
        glm::vec3 up = { 0.0f, 1.0f, 0.0f };

        glm::mat4 view = glm::lookAt(eye, target, up);
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = proj * view * model;

        gAudio.init();
        gAudio.loadEvent("block_destroy", "assets/sfx/destroy.wav");
        if (!recordCommandBuffers(ctx, 0.05f, 0.1f, 0.15f, &mvp[0][0], [&](VkCommandBuffer cb) {
            world.draw(ctx, cb);
            // draw the overlay into the same render pass
            dbgImGuiNewFrame();
            dbgImGuiDraw(ctx, cb, debugStats);
            })) throw std::runtime_error("record cmd buffers failed");
        if (!createSyncObjects(ctx)) throw std::runtime_error("sync objects failed");

        // For now, we won't create swapchain; just a running loop + device ready.
        std::cout << "Vulkan initialized. Running loop..." << std::endl;

        int cx = int(std::floor(cam.position.x / (CHUNK_SIZE * VOXEL_SCALE)));
        int cz = int(std::floor(cam.position.z / (CHUNK_SIZE * VOXEL_SCALE)));
        world.ensure(ctx, cx, cz, /*radius*/ 2); // 5x5 chunks
        worldUploadDirty(world, ctx);

        // debug: how many chunks and total tris?
        size_t chunks = world.map.size();
        size_t tris = 0;
        for (auto& kv : world.map) tris += kv.second->meshCPU.indices.size() / 3;
        std::cerr << "[World] created chunks=" << chunks << " tris=" << tris << "\n";

        double lastTime = glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            // Frame rate independent timing
            using clock = std::chrono::high_resolution_clock;
            static auto last = clock::now();
            static double acc = 0.0;
            static int frames = 0;

            auto now = clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now; acc += dt; frames++;

            if (acc >= 1.0) {
                double fps = frames / acc;
                char title[256];
                std::snprintf(title, sizeof(title), "VoxelGame  |  FPS: %.1f  |  AF: %.0fx | Edit: %s | Mat:%d",
                    fps, ctx.currentAniso, (editMode == EditMode::Small ? "Small" : "Big"), currentMaterial);
                glfwSetWindowTitle(window, title);   // returns void — just call it
                frames = 0; acc = 0.0;
            }

            //input.update(dt);

            // Debug Tools
            debugStats.worldRef = &world;
            debugStats.ctxRef = &ctx;
            dbgSetFrame(debugStats, dt);
            dbgSetCamera(debugStats, cam.position, cam.yaw, cam.pitch);
            dbgCollectWorldStats(world, debugStats);

            // Toggle F3 overlay (edge-triggered)
            static bool f3WasDown = false;                // persist across frames
            int f3State = glfwGetKey(window, GLFW_KEY_F3);
            if (f3State == GLFW_PRESS && !f3WasDown) {    // rising edge
                debugStats.overlay = !debugStats.overlay;
            }
            f3WasDown = (f3State == GLFW_PRESS);

            // Toggle anisotropic filtering level
            int fState = glfwGetKey(window, GLFW_KEY_F);
            if (fState == GLFW_PRESS && !fWasDown) {
                // rising edge
                const float LEVELS[] = { 1.0f, 4.0f, 8.0f, 16.0f };
                afIndex = (afIndex + 1) % (int)(sizeof(LEVELS) / sizeof(LEVELS[0]));

                float target = LEVELS[afIndex];
                if (!ctx.anisotropyFeature) target = 1.0f;
                target = std::min(target, ctx.maxSamplerAnisotropy);

                if (!recreateAtlasSampler(ctx, target)) {
                    std::cerr << "[VK] Recreate sampler failed\n";
                }
                else {
                    std::cerr << "[VK] AF set to " << target << "x\n";
                }
            }
            fWasDown = (fState == GLFW_PRESS);

            // Toggle world edit mode
            static bool bWasDown = false;
            int bState = glfwGetKey(window, GLFW_KEY_B);
            if (bState == GLFW_PRESS && !bWasDown) {
                editMode = (editMode == EditMode::Small) ? EditMode::Big : EditMode::Small;
                std::cerr << "[EDIT] mode = " << (editMode == EditMode::Small ? "Small" : "Big") << "\n";
            }
            bWasDown = (bState == GLFW_PRESS);

            if (g_scrollY != 0.0) {
                int steps = (g_scrollY > 0.0) ? 1 : -1;
                currentMaterial = clampMat(currentMaterial + steps);
                g_scrollY = 0.0;

                char title[256];
                std::snprintf(title, sizeof(title), "VoxelGame | Mat:%d", currentMaterial);
                glfwSetWindowTitle(window, title);
            }

            {
                static bool pPrev = false;
                bool pNow = glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS;
                if (pNow && !pPrev) {
                    physicsMode = !physicsMode;
                    player.physicsEnabled = physicsMode;
                    std::cerr << "[Player] physics " << (physicsMode ? "ON" : "OFF") << "\n";
                    // sync camera to player or vice versa on toggle
                    if (physicsMode) {
                        // put player under camera
                        player.pos = cam.position - glm::vec3(0, player.p.eyeOffset, 0);
                        player.vel = glm::vec3(0);
                    }
                    else {
                        // put camera at player eye
                        cam.position = player.camPosition();
                    }
                }
                pPrev = pNow;
            }

            if (physicsMode) {
                static bool spacePrev = false;
                bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
                if (space && !spacePrev && player.onGround) {
                    player.vel.y = player.p.jumpSpeed;
                    player.onGround = false;
                }
                spacePrev = space;
            }

            auto camF = glm::normalize(glm::vec3(std::cos(glm::radians(cam.yaw)), 0.0f,
                std::sin(glm::radians(cam.yaw))));
            auto camR = glm::normalize(glm::cross(camF, glm::vec3(0, 1, 0)));

            glm::vec3 wish(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += camF;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= camF;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += camR;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= camR;
            if (glm::length(wish) > 0.0f) wish = glm::normalize(wish);

            if (physicsMode) {
                player.simulate(world, wish, dt);
                cam.position = player.camPosition();  // camera follows player head
                // keep your existing cam yaw/pitch handling (mouse) for look
            }
            else {
                // your existing free-fly camera movement (use wish & keys to move cam directly)
                float flySpeed = 8.0f;
                float stepAmount = flySpeed * dt;   // both floats
                cam.position += wish * stepAmount;
                if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cam.position.y += flySpeed * dt;
                if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) cam.position.y -= flySpeed * dt;
            }

            static bool lWasDown = false, rWasDown = false;
            int l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
            int r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

            if ((l == GLFW_PRESS && !lWasDown) || (r == GLFW_PRESS && !rWasDown)) {
                // z kamery
                glm::vec3 p = cam.position;          // public field v tvojej FPSCamera
                glm::vec3 dir = cam.forward();    // smer poh?adu
                // else:
                float cy = glm::radians(cam.yaw), cp = glm::radians(cam.pitch);
                glm::vec3 dir2 = glm::normalize(glm::vec3(std::cos(cp) * std::cos(cy),
                    std::sin(cp),
                    std::cos(cp) * std::sin(cy)));

                RayHit hit = raycastWorld(world, cam.position, dir /*or dir2*/, pickMaxDist);
                if (hit.hit) {
                    bool changed = false;

                    if (l == GLFW_PRESS && !lWasDown) {
                        // remove the hit voxel (Small/Big depends on editMode)
                        changed |= worldEditSet(world, hit.vx, hit.vy, hit.vz, 0, editMode);
                    }
                    if (r == GLFW_PRESS && !rWasDown) {
                        // place next to the face we hit (use entry cell or normal offset)
                        int px = hit.vx + hit.nx;
                        int py = hit.vy + hit.ny;
                        int pz = hit.vz + hit.nz;
                        changed |= worldEditSet(world, px, py, pz, (BlockID)currentMaterial, editMode);
                    }

                    if (changed) {
                        // this walks chunks with wc->needsUpload==true and pushes new VBO/IBO
                        worldUploadDirty(world, ctx);
                    }
                }
            }

            lWasDown = (l == GLFW_PRESS);
            rWasDown = (r == GLFW_PRESS);

            glfwPollEvents();

            cam.handleMouse(window);
            cam.handleKeys(window, dt);

            glm::mat4 mvp = cam.mvp();
            if (!drawFrameWithMVP(ctx, &mvp[0][0], [&](VkCommandBuffer cb) {
                world.draw(ctx, cb);                 // binds per-chunk VBO/IBO and draws
                dbgImGuiNewFrame();                  // if you want overlay
                dbgImGuiDraw(ctx, cb, debugStats);
                })) {
                // Recreate swapchain on out-of-date
                int w = 0, h = 0;
                do { glfwGetFramebufferSize(window, &w, &h); glfwWaitEvents(); } while (w == 0 || h == 0);
                vkDeviceWaitIdle(ctx.device);
                destroyVoxelPipeline(ctx);
                cleanupSwapchain(ctx);
                if (!createSwapchain(ctx, (uint32_t)w, (uint32_t)h)) throw std::runtime_error("swapchain failed");
                if (!createImageViews(ctx)) throw std::runtime_error("image views failed");
                if (!createRenderPass(ctx)) throw std::runtime_error("render pass failed");
                if (!createDepthResources(ctx, ctx.swapchainExtent.width, ctx.swapchainExtent.height))
                    throw std::runtime_error("depth resources failed");
                if (!createFramebuffers(ctx)) throw std::runtime_error("framebuffers failed");
                if (!createCommandPoolAndBuffers(ctx)) throw std::runtime_error("cmd pool/buffers failed");
                if (!createVoxelPipeline(ctx, "shaders")) throw std::runtime_error("voxel pipeline failed");
                cam.setViewportSize(ctx.swapchainExtent.width, ctx.swapchainExtent.height); // <—
                // next loop drawFrameWithMVP will record with the new MVP

                // Re-init ImGui with the NEW render pass
                dbgImGuiShutdown(ctx);
                dbgImGuiInit(ctx, window);
            }
        }

        vkDeviceWaitIdle(ctx.device);

        // Cleanup
        if (ctx.device) vkDestroyDevice(ctx.device, nullptr);
        if (ctx.surface) vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);
        destroyDebug(ctx);
        if (ctx.materialUBOMem) vkFreeMemory(ctx.device, ctx.materialUBOMem, nullptr);
        if (ctx.materialUBO)    vkDestroyBuffer(ctx.device, ctx.materialUBO, nullptr);
        destroyVoxelMesh(ctx);
        destroyVoxelPipeline(ctx);
        cleanupSwapchain(ctx);
        if (ctx.instance) vkDestroyInstance(ctx.instance, nullptr);
        dbgImGuiShutdown(ctx);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
    catch (const std::exception& e) 
    {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
}
