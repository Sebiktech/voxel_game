#include "world/world_stream.hpp"
#include "world/world_gen2.hpp"
#include "world/mesher.hpp"
#include "world/world.hpp"
#include "vk_utils.hpp"
#include "settings.hpp"
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cmath>

static inline bool hasChunk(const World& w, const WorldKey& k) {
    return w.map.find(k) != w.map.end();
}

// create + generate + mesh + flag upload
static void createOne(World& w, VulkanContext& ctx, const WorldKey& k) {
    auto wc = std::make_unique<WorldChunk>();
    generateChunk(wc->data, { k.cx, k.cy, k.cz }, w.seed);
    wc->meshCPU = meshChunkAt(wc->data, k.cx, k.cy, k.cz);
    wc->needsUpload = true;
    w.map.emplace(k, std::move(wc));
    printf("[Stream] + chunk (%d,%d,%d)\n", k.cx, k.cy, k.cz);
}

int ensureChunkColumn(World& w, VulkanContext& ctx, int cx, int cz) {
    int made = 0;
    const int cyMin = 0, cyMax = 0; // adjust if you have stacked vertical chunks
    for (int cy = cyMin; cy <= cyMax; ++cy) {
        WorldKey k{ cx, cy, cz };
        if (!hasChunk(w, k)) {
            createOne(w, ctx, k);
            ++made;
        }
    }
    if (made) worldUploadDirty(w, ctx); // compact upload of anything flagged
    return made;
}

int streamEnsureAround(World& w, VulkanContext& ctx, int centerCx, int centerCz, int view) {
    int created = 0;
    // Optional: per-frame budget to avoid hitching
    int budget = 1000; // big number = eager; lower if you want throttling

    for (int dz = -view; dz <= view; ++dz) {
        for (int dx = -view; dx <= view; ++dx) {
            if (std::max(std::abs(dx), std::abs(dz)) > view) continue;
            if (budget <= 0) break;
            created += ensureChunkColumn(w, ctx, centerCx + dx, centerCz + dz);
            budget -= created; // crude budget; you can subtract 1 per ensure call if you prefer
        }
        if (budget <= 0) break;
    }
    return created;
}

int streamUnloadFar(World& w, int centerCx, int centerCz, int view) {
    std::vector<WorldKey> toErase;
    toErase.reserve(w.map.size());
    for (auto const& kv : w.map) {
        const WorldKey& k = kv.first;
        int dx = k.cx - centerCx, dz = k.cz - centerCz;
        if (std::max(std::abs(dx), std::abs(dz)) > view)
            toErase.push_back(k);
    }
    for (auto& k : toErase) {
        printf("[Stream] - chunk (%d,%d,%d)\n", k.cx, k.cy, k.cz);
        w.destroyChunk(k);
    }
    return (int)toErase.size();
}

// ===== THE MISSING FUNCTION THAT TIES EVERYTHING TOGETHER =====
void worldStreamTick(World& w, VulkanContext& ctx,
    const glm::vec3& camPos, const glm::vec3& /*camFwd*/) {

    // Convert player world position to chunk coordinates
    // camPos is in world space (scaled by VOXEL_SCALE = 0.25)

    // Step 1: World space -> Voxel space
    const int vx = (int)std::floor(camPos.x / VOXEL_SCALE + 0.5f);
    const int vz = (int)std::floor(camPos.z / VOXEL_SCALE + 0.5f);

    // Step 2: Voxel space -> Chunk space (using floor division for negatives)
    auto floordiv = [](int a, int b) -> int {
        int q = a / b;
        int r = a % b;
        return (r && ((r < 0) != (b < 0))) ? (q - 1) : q;
        };

    const int cx = floordiv(vx, CHUNK_SIZE);
    const int cz = floordiv(vz, CHUNK_SIZE);

    // Use gViewDist and gUnloadSlack from settings
    const int viewRadius = gViewDist;
    const int keepRadius = gViewDist + gUnloadSlack;

    // Debug output every 2 seconds (at 60fps)
    static int debugTick = 0;
    if (debugTick++ % 120 == 0) {
        printf("[Stream] Player at world(%.2f, %.1f, %.2f) -> voxel(%d, %d) -> chunk(%d, %d) | loaded=%zu\n",
            camPos.x, camPos.y, camPos.z, vx, vz, cx, cz, w.map.size());
    }

    // Load chunks around player position
    int loaded = streamEnsureAround(w, ctx, cx, cz, viewRadius);

    // Unload chunks beyond keep radius
    int unloaded = streamUnloadFar(w, cx, cz, keepRadius);

    // Optional: Log when chunks are created/destroyed
    if (loaded > 0 || unloaded > 0) {
        printf("[Stream] Tick: loaded=%d, unloaded=%d, total=%zu\n",
            loaded, unloaded, w.map.size());
    }
}