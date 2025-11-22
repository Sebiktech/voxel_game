#include "world/world_stream.hpp"
#include "world/world_gen2.hpp"
#include "world/mesher.hpp"
#include <algorithm>
#include <vector>
#include <cstdio>

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
