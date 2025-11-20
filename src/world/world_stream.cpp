#include "world/world_stream.hpp"
#include "world/world_gen2.hpp"   // generateChunk(...)
#include "world/mesher.hpp"       // meshChunkAt(...)
#include "world/world_config.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

static inline bool hasChunk(const World& w, const WorldKey& k) {
    return w.map.find(k) != w.map.end();
}

// Create ONE chunk if missing: generate -> mesh -> mark for upload
static void ensureOne(World& w, VulkanContext& ctx, const WorldKey& k) {
    if (hasChunk(w, k)) return;

    auto wc = std::make_unique<WorldChunk>();

    // CPU generate
    generateChunk(wc->data, { k.cx, k.cy, k.cz }, w.seed);

    // CPU mesh with world coords offset
    wc->meshCPU = meshChunkAt(wc->data, k.cx, k.cy, k.cz);

    // mark for upload; we’ll batch-upload below
    wc->needsUpload = true;

    w.map.emplace(k, std::move(wc));
}

bool ensureChunkColumn(World& w, VulkanContext& ctx, int cx, int cz) {
    bool any = false;
    const int cyMin = 0, cyMax = 0; // adjust if you stack vertical chunks
    for (int cy = cyMin; cy <= cyMax; ++cy) {
        WorldKey k{ cx, cy, cz };
        if (!hasChunk(w, k)) {
            ensureOne(w, ctx, k);
            any = true;
        }
    }
    // If any created, upload all pending (reuses your worldUploadDirty)
    if (any) worldUploadDirty(w, ctx);  // defined in world.cpp
    return any;
}

void streamEnsureAround(World& w, VulkanContext& ctx, int centerCx, int centerCz, int view) {
    for (int dz = -view; dz <= view; ++dz) {
        for (int dx = -view; dx <= view; ++dx) {
            int cx = centerCx + dx;
            int cz = centerCz + dz;
            if (std::max(std::abs(dx), std::abs(dz)) > view) continue;
            ensureChunkColumn(w, ctx, cx, cz);
        }
    }
}

void streamUnloadFar(World& w, int centerCx, int centerCz, int view) {
    std::vector<WorldKey> toErase;
    toErase.reserve(w.map.size());
    for (auto const& kv : w.map) {
        const WorldKey& k = kv.first;
        int dx = k.cx - centerCx;
        int dz = k.cz - centerCz;
        if (std::max(std::abs(dx), std::abs(dz)) > view + 1) {
            toErase.push_back(k);
        }
    }
    for (auto& k : toErase) w.destroyChunk(k);
}