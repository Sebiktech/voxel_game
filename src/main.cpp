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
#include "world/world_stream.hpp"
#include "player.hpp"
#include "settings.hpp"


float pickMaxDist = 8.0f;
bool physicsMode = true; 
static bool g_uiMode = false;  // true = UI focus (mouse free), false = Game focus (mouse captured)
static bool g_escWasDown = false;
//static Input input;
static DebugStats debugStats;
EditMode editMode = EditMode::Small;
FPSCamera cam;
Player player;
World world;
VulkanContext ctx{};
static Audio gAudio;

static int   afIndex = 0;
static bool  fWasDown = false;                            // edge detector
static bool  f3WasDown = false;
int currentMaterial = 1;

static int worldToChunkCoord(float w) {
    int v = (int)std::floor(w);
    int q = v / CHUNK_SIZE, r = v % CHUNK_SIZE;
    if (r < 0) { r += CHUNK_SIZE; --q; }
    return q;
}

// reacts to camera or view-distance changes
static void streamTick(World& world, VulkanContext& ctx, const FPSCamera& cam) {
    static int lastCx = INT_MAX, lastCz = INT_MAX, lastView = -1;

    const int cx = worldToChunkCoord(cam.position.x);
    const int cz = worldToChunkCoord(cam.position.z);

    const bool moved = (cx != lastCx) || (cz != lastCz);
    const bool viewChanged = (gViewDist != lastView);

    if (!moved && !viewChanged) return;

    printf("[Stream] center=(%d,%d) view=%d%s\n",
        cx, cz, gViewDist, viewChanged ? " (changed)" : "");

    // load  - only around camera
    int created = streamEnsureAround(world, ctx, cx, cz, world.stream.viewRadius);

    // unload - everything beyond view + slack
    int destroyed = streamUnloadFar(world, cx, cz, world.stream.keepRadius);

    printf("[Stream] created=%d destroyed=%d loadedNow=%zu\n",
        created, destroyed, world.map.size());

    lastCx = cx; lastCz = cz; lastView = gViewDist;
}

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

static void framebufferResizeCallback(GLFWwindow* win, int width, int height) {
    auto* ctx = reinterpret_cast<VulkanContext*>(glfwGetWindowUserPointer(win));
    if (!ctx) return;
    ctx->framebufferResized = true;    // mark for recreate
}

// at top-scope of main.cpp
static double g_scrollY = 0.0;
static void scroll_cb(GLFWwindow*, double /*xoff*/, double yoff) { g_scrollY += yoff; }

void initGame() {
    // Set view distance from settings
    gViewDist = 5;      // Load chunks within 8 chunks (17x17 grid)
    gUnloadSlack = 0;   // Keep chunks up to 10 chunks away before unloading

    // Initialize world
    world.seed = 12345;

    // Set player spawn position (in world space)
    // If you want to spawn at voxel (0, 64, 0):
    // world_pos = voxel * VOXEL_SCALE = (0, 64, 0) * 0.25 = (0, 16, 0)
    player.pos = glm::vec3(0.0f, 16.0f, 0.0f); // 64 voxels high
    player.vel = glm::vec3(0.0f);

    // Initialize camera at player position
    cam.position = player.camPosition();

    // Pre-load initial chunks around spawn
    printf("Pre-loading initial chunks...\n");
    const int vx = (int)std::floor(player.pos.x / VOXEL_SCALE + 0.5f);
    const int vz = (int)std::floor(player.pos.z / VOXEL_SCALE + 0.5f);

    auto floordiv = [](int a, int b) -> int {
        int q = a / b;
        int r = a % b;
        return (r && ((r < 0) != (b < 0))) ? (q - 1) : q;
        };

    const int spawnCx = floordiv(vx, CHUNK_SIZE);
    const int spawnCz = floordiv(vz, CHUNK_SIZE);

    printf("Spawn at chunk (%d, %d)\n", spawnCx, spawnCz);
    int loaded = streamEnsureAround(world, ctx, spawnCx, spawnCz, gViewDist);
    printf("Pre-loaded %d chunks\n", loaded);

    // Wait for GPU uploads
    vkDeviceWaitIdle(ctx.device);
}

void updateGame(GLFWwindow* window, float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();

    // Block gameplay input if:
    //  - you're in UI mode (Esc toggled), OR
    //  - ImGui wants the device (hovering widgets, active text box, etc.)
    const bool blockMouse = g_uiMode || io.WantCaptureMouse;
    const bool blockKeys = g_uiMode || io.WantCaptureKeyboard;

    // 1. Update camera from input
    if (!blockMouse) cam.handleMouse(window);
    if (!blockKeys) cam.handleKeys(window, deltaTime);

    // 2. Get movement direction from camera (for player physics)
    glm::vec3 wishDir = glm::vec3(0.0f);
    if (player.physicsEnabled) {
        // Convert camera-relative WASD to world direction
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += cam.forward();
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= cam.forward();
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += cam.right();
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= cam.right();

        // Flatten to XZ plane for ground movement
        wishDir.y = 0.0f;
        if (glm::length(wishDir) > 0.0f) {
            wishDir = glm::normalize(wishDir);
        }

        // Jump
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround) {
            player.vel.y = player.p.jumpSpeed;
        }
    }

    // 3. Update player physics
    player.simulate(world, wishDir, deltaTime);

    // 4. Sync camera to player
    if (player.physicsEnabled) {
        cam.position = player.camPosition();
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


    glm::vec3 wish(0.0f);

    if (!blockKeys)
    {
        if (physicsMode) {
            cam.position = player.camPosition();  // camera follows player head
            // keep your existing cam yaw/pitch handling (mouse) for look
        }
        else {
            // your existing free-fly camera movement (use wish & keys to move cam directly)
            float flySpeed = 8.0f;
            float stepAmount = flySpeed * deltaTime;   // both floats
            cam.position += wish * stepAmount;
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cam.position.y += flySpeed * deltaTime;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) cam.position.y -= flySpeed * deltaTime;
        }
    }

    // 5. *** CRITICAL: Update chunk streaming based on player position ***
    worldStreamTick(world, ctx, player.pos, cam.forward());

    // 6. Other game logic (raycast for block editing, etc.)

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

    // Toggle physics mode
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

    // Toggle world edit mode
    static bool bWasDown = false;
    int bState = glfwGetKey(window, GLFW_KEY_B);
    if (bState == GLFW_PRESS && !bWasDown) {
        editMode = (editMode == EditMode::Small) ? EditMode::Big : EditMode::Small;
        std::cerr << "[EDIT] mode = " << (editMode == EditMode::Small ? "Small" : "Big") << "\n";
    }
    bWasDown = (bState == GLFW_PRESS);

    auto clampMat = [&](int m) {
        if (m < 1) m = MAX_MATERIALS - 1;                 // wrap
        if (m >= MAX_MATERIALS) m = 1;                    // wrap
        return m;
        };

    if (g_scrollY != 0.0) {
        int steps = (g_scrollY > 0.0) ? 1 : -1;
        currentMaterial = clampMat(currentMaterial + steps);
        g_scrollY = 0.0;

        char title[256];
        std::snprintf(title, sizeof(title), "VoxelGame | Mat:%d", currentMaterial);
        glfwSetWindowTitle(window, title);
    }

    // --- UI focus toggle with ESC ---
    int esc = glfwGetKey(window, GLFW_KEY_ESCAPE);
    if (esc == GLFW_PRESS && !g_escWasDown) {
        g_uiMode = !g_uiMode;
        std::cout << g_uiMode;

        // capture or release cursor for your FPS camera
        cam.setCursorCaptured(window, !g_uiMode); // true = lock/hide, false = free cursor

        // hard refocus the window (important after fullscreen switches)
        glfwFocusWindow(window);
        //ImGui::ClearActiveID();
    }
    g_escWasDown = (esc == GLFW_PRESS);

    if (!blockKeys)
    {

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

    }
}

void renderGame(VkCommandBuffer cmd) {
    // Bind your pipeline, descriptors, etc.

    // Update push constants with camera MVP
}

int main() 
{
    try {
        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");
        if (!glfwVulkanSupported()) throw std::runtime_error("GLFW: Vulkan not supported");

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(1920, 1080, "Voxel Game (Starter)", nullptr, nullptr);
        if (!window) throw std::runtime_error("Failed to create window");

        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        //input.init(window);
        glfwSetScrollCallback(window, scroll_cb);

        ctx.window = window;
        glfwSetWindowUserPointer(window, &ctx);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

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
        cam.setViewportSize(ctx.swapchainExtent.width, ctx.swapchainExtent.height);
        cam.setCursorCaptured(window, !g_uiMode); // keep cursor mode consistent
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
        if (!createLightingUBO(ctx))   throw std::runtime_error("lighting UBO failed");
        if (!createDescriptors(ctx))                               // <— makes descSetLayout
            throw std::runtime_error("descriptors failed");
        if (!createSkyPipeline(ctx, "shaders")) throw std::runtime_error("sky pipeline failed");
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

        auto worldToChunk = [](float w) {
            int v = (int)std::floor(w);
            int q = v / CHUNK_SIZE, r = v % CHUNK_SIZE;
            if (r < 0) { r += CHUNK_SIZE; --q; }
            return q;
            };

        static int lastCamCx = 1e9, lastCamCz = 1e9;
        static int lastView = -1;

        int camCx = worldToChunk(cam.position.x);
        int camCz = worldToChunk(cam.position.z);

        if (camCx != lastCamCx || camCz != lastCamCz || gViewDist != lastView) {
            streamEnsureAround(world, ctx, camCx, camCz, world.stream.viewRadius);
            streamUnloadFar(world, camCx, camCz, world.stream.keepRadius);

            lastCamCx = camCx; lastCamCz = camCz;
            lastView = gViewDist;
        }
        //world.ensure(ctx, cx, cz, /*radius*/ 2); // 5x5 chunks
        worldUploadDirty(world, ctx);

        // debug: how many chunks and total tris?
        size_t chunks = world.map.size();
        size_t tris = 0;
        for (auto& kv : world.map) tris += kv.second->meshCPU.indices.size() / 3;
        std::cerr << "[World] created chunks=" << chunks << " tris=" << tris << "\n";

        initGame();

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

            // Debug Tools
            debugStats.worldRef = &world;
            debugStats.ctxRef = &ctx;
            dbgSetFrame(debugStats, dt);
            dbgSetCamera(debugStats, cam.position, cam.yaw, cam.pitch);
            dbgCollectWorldStats(world, debugStats);

            streamTick(world, ctx, cam);

            glfwPollEvents();

            updateGame(window, dt);

            glm::mat4 mvp = cam.mvp();
            // At the start of each frame (before drawing)
            auto recreateSwapchainAll = [&]() {
                int w = 0, h = 0;
                do { glfwGetFramebufferSize(window, &w, &h); glfwWaitEvents(); } while (w == 0 || h == 0);

                vkDeviceWaitIdle(ctx.device);

                // Tear down GPU stuff tied to swapchain
                destroyVoxelPipeline(ctx);
                cleanupSwapchain(ctx);                  // destroys fbos, rp, views, swapchain, depth, cmd pool, semaphores & fence

                // Recreate swapchain-sized resources
                if (!createSwapchain(ctx, (uint32_t)w, (uint32_t)h)) throw std::runtime_error("swapchain failed");
                if (!createImageViews(ctx)) throw std::runtime_error("image views failed");
                if (!createRenderPass(ctx)) throw std::runtime_error("render pass failed");
                if (!createDepthResources(ctx, ctx.swapchainExtent.width, ctx.swapchainExtent.height))
                    throw std::runtime_error("depth resources failed");
                if (!createFramebuffers(ctx)) throw std::runtime_error("framebuffers failed");
                if (!createCommandPoolAndBuffers(ctx))
                    throw std::runtime_error("cmd pool/buffers failed");

                if (!createSkyPipeline(ctx, "shaders")) throw std::runtime_error("sky pipeline failed");
                // Recreate pipeline (depends on render pass & extent)
                if (!createVoxelPipeline(ctx, "shaders")) throw std::runtime_error("voxel pipeline failed");

                // Recreate sync objects (cleanupSwapchain destroyed them)
                if (!createSyncObjects(ctx)) throw std::runtime_error("sync objects failed");

                // Re-init ImGui backend with the NEW render pass
                if (!dbgImGuiReinit(ctx, window)) throw std::runtime_error("ImGui reinit failed");

                // Update camera projection
                cam.setViewportSize(ctx.swapchainExtent.width, ctx.swapchainExtent.height);

                // Keep cursor state consistent
                cam.setCursorCaptured(window, !g_uiMode);

                ctx.framebufferResized = false;
                };

            if (ctx.framebufferResized) {
                recreateSwapchainAll();
            }

            if (!drawFrameWithMVP(ctx, &mvp[0][0], [&](VkCommandBuffer cb) {
                world.draw(ctx, cb);                 // binds per-chunk VBO/IBO and draws
                dbgImGuiNewFrame();                  // if you want overlay
                dbgImGuiDraw(ctx, cb, debugStats);
                })) {
                recreateSwapchainAll();
                continue; // next frame
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
        destroySkyPipeline(ctx);
        destroyVoxelPipeline(ctx);
        cleanupSwapchain(ctx);
        if (ctx.instance) vkDestroyInstance(ctx.instance, nullptr);
        dbgImGuiShutdown();
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
