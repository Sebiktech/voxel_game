#include "world/biomes/biome_forest.hpp"

static inline float r1(int x, int z, uint32_t seed, float f) {
    uint32_t h = (uint32_t)(x * 2654435761u) ^ (uint32_t)(z * 97531u) ^ seed;
    h ^= (h << 13); h ^= (h >> 17); h ^= (h << 5);
    float u = (h & 0xFFFF) / 65535.0f;
    return (u * 2.f - 1.f) * f;
}

BiomeSample BiomeForest::sample(int x, int z, uint32_t seed) const {
    float h = base + r1((int)(x * freq), (int)(z * freq), seed ^ 0x3333u, amp);
    return { h, /*surface*/ 3 /* forest surface */ };
}