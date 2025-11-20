#include "world/biome_map.hpp"
#include <algorithm>

static inline float n01(int x, int z, uint32_t seed, float freq) {
    uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(z * 19349663) ^ seed;
    h ^= (h >> 13); h *= 0x5bd1e995u; h ^= (h >> 15);
    return (h & 0xFFFF) / 65535.0f;
}
static inline float smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.f - 2.f * t);
}

BiomeSample BiomeMap::blended(int x, int z, uint32_t seed) const {
    // “climate” coords
    float T = n01((int)(x * 0.0015f), (int)(z * 0.0015f), seed ^ 0x4444u, 1.0f);
    float M = n01((int)(x * 0.0012f), (int)(z * 0.0012f), seed ^ 0x5555u, 1.0f);

    // soften borders
    T = smoothstep(0.2f, 0.8f, T);
    M = smoothstep(0.2f, 0.8f, M);

    // 4-corner biome grid
    BiomeSample b00 = plain.sample(x, z, seed); // (T=0,M=0)
    BiomeSample b10 = hills.sample(x, z, seed); // (T=1,M=0)
    BiomeSample b01 = forest.sample(x, z, seed); // (T=0,M=1)
    BiomeSample b11 = hills.sample(x, z, seed); // (T=1,M=1)

    // bilinear blend heights
    float h0 = b00.height * (1 - T) + b10.height * T;
    float h1 = b01.height * (1 - T) + b11.height * T;
    float H = h0 * (1 - M) + h1 * M;

    // choose surface from the strongest corner weight (simple & stable)
    float w00 = (1 - T) * (1 - M), w10 = T * (1 - M), w01 = (1 - T) * M, w11 = T * M;
    float wmax = w00; uint16_t sid = b00.surfaceId;
    if (w10 > wmax) { wmax = w10; sid = b10.surfaceId; }
    if (w01 > wmax) { wmax = w01; sid = b01.surfaceId; }
    if (w11 > wmax) { wmax = w11; sid = b11.surfaceId; }

    return { H, sid };
}