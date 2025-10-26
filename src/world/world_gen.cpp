#include <cmath>
#include <cstdint>
#include "world/world_config.hpp"
#include "world/chunk.hpp"
#include <algorithm>

// --- malý value-noise (deterministický), bez externých závislostí ---
static inline uint32_t wanghash(uint32_t x) {
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2d;
    x = x ^ (x >> 15);
    return x;
}
static inline float rand01(int x, int z, uint32_t seed = 1337u) {
    return (wanghash((uint32_t)x * 73856093u ^ (uint32_t)z * 19349663u ^ seed) & 0xFFFFFF) / float(0xFFFFFF);
}
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth(float t) { return t * t * (3.f - 2.f * t); }

// 2D "value noise" (bilineárny, vyhladený)
static float valueNoise2D(float x, float z, float freq, uint32_t seed = 1337u) {
    x *= freq; z *= freq;
    int xi = (int)std::floor(x);
    int zi = (int)std::floor(z);
    float tx = smooth(x - xi);
    float tz = smooth(z - zi);

    float v00 = rand01(xi + 0, zi + 0, seed);
    float v10 = rand01(xi + 1, zi + 0, seed);
    float v01 = rand01(xi + 0, zi + 1, seed);
    float v11 = rand01(xi + 1, zi + 1, seed);

    float vx0 = lerp(v00, v10, tx);
    float vx1 = lerp(v01, v11, tx);
    return lerp(vx0, vx1, tz); // 0..1
}

// --- API implementácie ---

void generateFlatChunk(Chunk& c, int baseBlocks, uint16_t blockId) {
    // škálovanie výšok, aby fyzika ostala rovnaká
    const int H = std::clamp(baseBlocks * VOXEL_HEIGHT_SCALE, 0, CHUNK_HEIGHT);

    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x)
            for (int y = 0; y < H; ++y) {
                c.set(x, y, z, blockId);
            }
}

void generateHeightmapChunk(Chunk& c, int baseH, int amp, float freq,
    uint16_t topId, uint16_t dirtId)
{
    // škálovanie, aby to zodpovedalo pôvodnej fyzike
    const int baseScaled = std::clamp(baseH * VOXEL_HEIGHT_SCALE, 0, CHUNK_HEIGHT);
    const int ampScaled = std::max(0, amp * VOXEL_HEIGHT_SCALE);

    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            float n = valueNoise2D((float)x, (float)z, freq, 1337u); // 0..1
            int h = baseScaled + int(std::round(n * ampScaled));
            h = std::clamp(h, 0, CHUNK_HEIGHT - 1);

            for (int y = 0; y <= h; ++y) {
                uint16_t id = (y == h ? topId : dirtId);
                c.set(x, y, z, id);
            }
        }
    }
}