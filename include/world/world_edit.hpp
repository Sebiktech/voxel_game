#pragma once
#include "world.hpp"
#include "mesher.hpp"
#include <array>

enum class EditMode { Small, Big };

inline int snapToRegular(int c) { return (c >> 1) << 1; } // align to even

inline int floordiv_i(int a, int b) {
    int q = a / b;
    int r = a % b;
    return (r && ((r < 0) != (b < 0))) ? (q - 1) : q;
}
inline int floormod_i(int a, int b) {
    int m = a % b;
    if (m < 0) m += (b < 0 ? -b : b);
    return m;
}

// Set one smallest voxel by WORLD coords if its chunk is loaded.
// Returns pointer to the edited WorldChunk, or nullptr if not loaded.
inline WorldChunk* worldSetOne(World& w, int vx, int vy, int vz, BlockID id) {
    int cx = floordiv_i(vx, CHUNK_SIZE);
    int cy = floordiv_i(vy, CHUNK_HEIGHT);
    int cz = floordiv_i(vz, CHUNK_SIZE);
    int lx = floormod_i(vx, CHUNK_SIZE);
    int ly = floormod_i(vy, CHUNK_HEIGHT);
    int lz = floormod_i(vz, CHUNK_SIZE);

    WorldKey k{ cx, cy, cz };
    auto it = w.map.find(k);
    if (it == w.map.end()) return nullptr; // not loaded

    WorldChunk* wc = it->second.get();
    wc->data.set(lx, ly, lz, id);
    return wc;
}

// Rebuild mesh for a chunk and mark it for GPU upload (CPU-only step here).
inline void rebuildAndMark(WorldChunk* wc) {
    if (!wc) return;
    wc->meshCPU = meshChunk(wc->data);   // whole-chunk rebuild; swap to meshChunkRegion later if desired
    wc->needsUpload = true;
}

// Edit entry point: world voxel coords + mode (Small/Big).
// Returns true if any loaded chunk was modified.
inline bool worldEditSet(World& w, int vx, int vy, int vz, BlockID id, EditMode mode)
{
    bool changed = false;

    if (mode == EditMode::Small) {
        if (auto* wc = worldSetOne(w, vx, vy, vz, id)) {
            rebuildAndMark(wc);
            changed = true;
        }
        return changed;
    }

    // Big (2x2x2) block aligned to even coordinates in WORLD space
    int bx = snapToRegular(vx);
    int by = snapToRegular(vy);
    int bz = snapToRegular(vz);

    // Collect up to 8 unique chunks touched
    std::array<WorldChunk*, 8> touched{}; int nTouched = 0;

    for (int dz = 0; dz < BIG_BLOCK_SIZE; ++dz)
        for (int dy = 0; dy < BIG_BLOCK_SIZE; ++dy)
            for (int dx = 0; dx < BIG_BLOCK_SIZE; ++dx) {
                int wx = bx + dx;
                int wy = by + dy;
                int wz = bz + dz;

                if (auto* wc = worldSetOne(w, wx, wy, wz, id)) {
                    // dedup chunk pointers
                    bool seen = false;
                    for (int i = 0; i < nTouched; i++) if (touched[i] == wc) { seen = true; break; }
                    if (!seen && nTouched < (int)touched.size()) touched[nTouched++] = wc;
                    changed = true;
                }
            }

    for (int i = 0; i < nTouched; i++) rebuildAndMark(touched[i]);
    return changed;
}