#pragma once
#include <array>
#include <cstdint>
#include "world.hpp"          // World, WorldKey, WorldChunk, world.map
#include "mesher.hpp"         // meshChunkAt(...)
#include "world_config.hpp"   // CHUNK_SIZE, CHUNK_HEIGHT, BlockID, etc.

// Edit mode
enum class EditMode { Small, Big };

// ----- integer floor-div/mod that work for negatives -----
inline int floordiv_i(int a, int b) {
    int q = a / b, r = a % b;
    return (r && ((r < 0) != (b < 0))) ? (q - 1) : q;
}
inline int floormod_i(int a, int b) {
    int m = a % b;
    if (m < 0) m += (b < 0 ? -b : b);
    return m;
}
inline int snapToEven(int v) { return (v >> 1) << 1; }  // align to 2

// Find loaded chunk by chunk coords
inline WorldChunk* findChunk(World& w, int cx, int cy, int cz) {
    WorldKey k{ cx, cy, cz };
    auto it = w.map.find(k);               // adjust container name if different
    return (it == w.map.end()) ? nullptr : it->second.get();
}

// Write one voxel by WORLD coords (returns touched chunk or nullptr if not loaded)
inline WorldChunk* worldSetOne(World& w, int wx, int wy, int wz, BlockID id) {
    const int cx = floordiv_i(wx, CHUNK_SIZE);
    const int cy = floordiv_i(wy, CHUNK_HEIGHT);
    const int cz = floordiv_i(wz, CHUNK_SIZE);

    const int lx = floormod_i(wx, CHUNK_SIZE);
    const int ly = floormod_i(wy, CHUNK_HEIGHT);
    const int lz = floormod_i(wz, CHUNK_SIZE);

    WorldChunk* wc = findChunk(w, cx, cy, cz);
    if (!wc) return nullptr;

    if (lx < 0 || lx >= CHUNK_SIZE ||
        ly < 0 || ly >= CHUNK_HEIGHT ||
        lz < 0 || lz >= CHUNK_SIZE) return wc;

    // IMPORTANT: use Chunk::set(), not operator()
    wc->data.set(lx, ly, lz, id);
    return wc;
}

// Rebuild CPU mesh with baked world offset (meshChunkAt) and mark for upload
inline void rebuildAndMarkAt(WorldChunk* wc, int cx, int cy, int cz) {
    if (!wc) return;
    wc->meshCPU = meshChunkAt(wc->data, cx, cy, cz);  // correct world position
    wc->needsUpload = true;
}

// Main edit entry: world coords + mode. Returns true if any change applied.
inline bool worldEditSet(World& w, int wx, int wy, int wz, BlockID id, EditMode mode) {
    bool changed = false;

    if (mode == EditMode::Small) {
        // compute chunk coords once and pass to rebuild
        const int cx = floordiv_i(wx, CHUNK_SIZE);
        const int cy = floordiv_i(wy, CHUNK_HEIGHT);
        const int cz = floordiv_i(wz, CHUNK_SIZE);

        if (WorldChunk* wc = worldSetOne(w, wx, wy, wz, id)) {
            rebuildAndMarkAt(wc, cx, cy, cz);
            changed = true;
        }
        return changed;
    }

    // Big: 2x2x2 aligned in WORLD space (can straddle chunk borders)
    const int bx = snapToEven(wx);
    const int by = snapToEven(wy);
    const int bz = snapToEven(wz);

    struct Touched { WorldChunk* wc; int cx, cy, cz; };
    std::array<Touched, 8> touched{};
    int nTouched = 0;

    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const int vx = bx + dx;
                const int vy = by + dy;
                const int vz = bz + dz;

                const int cx = floordiv_i(vx, CHUNK_SIZE);
                const int cy = floordiv_i(vy, CHUNK_HEIGHT);
                const int cz = floordiv_i(vz, CHUNK_SIZE);

                if (WorldChunk* wc = worldSetOne(w, vx, vy, vz, id)) {
                    // de-duplicate touched chunks
                    bool seen = false;
                    for (int i = 0; i < nTouched; ++i) if (touched[i].wc == wc) { seen = true; break; }
                    if (!seen && nTouched < (int)touched.size()) touched[nTouched++] = { wc, cx, cy, cz };
                    changed = true;
                }
            }

    // Remesh each touched chunk with the coords we tracked
    for (int i = 0; i < nTouched; ++i) {
        rebuildAndMarkAt(touched[i].wc, touched[i].cx, touched[i].cy, touched[i].cz);
    }

    return changed;
}