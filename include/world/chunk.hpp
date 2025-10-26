#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "world_config.hpp"

constexpr int CHUNK_SIZE = 64;
constexpr int CHUNK_HEIGHT = 1024;

// ADD THIS HERE so everything is constexpr in one TU
constexpr int REGION_SIZE = 32;
static_assert(CHUNK_SIZE% REGION_SIZE == 0, "REGION_SIZE must divide CHUNK_SIZE");
static_assert(CHUNK_HEIGHT% REGION_SIZE == 0, "REGION_SIZE must divide CHUNK_HEIGHT");

constexpr int REGIONS_X = CHUNK_SIZE / REGION_SIZE;  // 64/32 = 2
constexpr int REGIONS_Y = CHUNK_HEIGHT / REGION_SIZE;  // 1024/32 = 32
constexpr int REGIONS_Z = CHUNK_SIZE / REGION_SIZE;  // 64/32 = 2
constexpr int REGION_COUNT = REGIONS_X * REGIONS_Y * REGIONS_Z;

inline int regionIndex(int rx, int ry, int rz) {
    return rx + REGIONS_X * (rz + REGIONS_Z * ry);
}
inline int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

using BlockID = uint16_t;

struct Chunk {
    std::vector<BlockID> blocks;
    Chunk() : blocks(CHUNK_SIZE* CHUNK_HEIGHT* CHUNK_SIZE, 0) {}

    static inline int index(int x, int y, int z) {
        return x + CHUNK_SIZE * (z + CHUNK_SIZE * y);
    }
    inline bool inBounds(int x, int y, int z) const {
        return (x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE && y < CHUNK_HEIGHT && z < CHUNK_SIZE);
    }
    inline BlockID get(int x, int y, int z) const {
        return blocks[index(x, y, z)];
    }
    inline void set(int x, int y, int z, BlockID id) {
        blocks[index(x, y, z)] = id;
    }
};

struct MeshData {
    // 10 floats/vertex: pos(3) + normal(3) + uv(2) + tile(2)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

// zaisti?, že indexy sedia do chunku
inline bool inChunk(int x, int y, int z) {
    return (x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE && y < CHUNK_HEIGHT && z < CHUNK_SIZE);
}

// zaokrúhlenie "dole" na za?iatok 2×2×2 bloku
inline int bigOriginCoord(int c) { return (c >> 1) << 1; } // (c/2)*2

// set 1 malý voxel (bezpe?ne)
inline void setSmallSafe(Chunk& c, int x, int y, int z, BlockID id) {
    if (inChunk(x, y, z)) c.set(x, y, z, id);
}

// nastav celý ve?ký blok pod?a ?UBOVO?NÉHO malého voxelu v ?om
inline void setBigByAnyVoxel(Chunk& c, int x, int y, int z, BlockID id) {
    int bx = bigOriginCoord(x);
    int by = bigOriginCoord(y);
    int bz = bigOriginCoord(z);
    for (int dz = 0; dz < BIG_BLOCK_SIZE; ++dz)
        for (int dy = 0; dy < BIG_BLOCK_SIZE; ++dy)
            for (int dx = 0; dx < BIG_BLOCK_SIZE; ++dx) {
                setSmallSafe(c, bx + dx, by + dy, bz + dz, id);
            }
}

// vymaž (nastav na 0) celý ve?ký blok pod?a ?UBOVO?NÉHO malého voxelu v ?om
inline void clearBigByAnyVoxel(Chunk& c, int x, int y, int z) {
    setBigByAnyVoxel(c, x, y, z, 0);
}

// zistí, ?i je celý ve?ký blok uniformný (všetkých 8 rovnakých)
// vráti id, a do 'allSame' dá true/false; ak nie je uniformný, vráti id z [bx,by,bz]
inline BlockID getBigInfo(const Chunk& c, int x, int y, int z, bool& allSame) {
    int bx = bigOriginCoord(x);
    int by = bigOriginCoord(y);
    int bz = bigOriginCoord(z);
    BlockID first = 0;
    allSame = true;
    for (int dz = 0; dz < BIG_BLOCK_SIZE; ++dz)
        for (int dy = 0; dy < BIG_BLOCK_SIZE; ++dy)
            for (int dx = 0; dx < BIG_BLOCK_SIZE; ++dx) {
                BlockID v = inChunk(bx + dx, by + dy, bz + dz) ? c.get(bx + dx, by + dy, bz + dz) : 0;
                if (dx == 0 && dy == 0 && dz == 0) first = v;
                else if (v != first) allSame = false;
            }
    return first;
}