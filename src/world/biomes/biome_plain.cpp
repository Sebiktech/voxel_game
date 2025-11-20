#include "world/biomes/biome_plain.hpp"

// super light hash noise (cheap)
static inline float n2(int x, int z, uint32_t seed, float f) {
    uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(z * 19349663) ^ seed;
    h ^= (h >> 13); h *= 0x5bd1e995u; h ^= (h >> 15);
    float u = (h & 0xFFFF) / 65535.0f;
    return (u * 2.f - 1.f) * f;
}

BiomeSample BiomePlain::sample(int x, int z, uint32_t seed) const {
    float h = base + n2((int)(x * freq), (int)(z * freq), seed ^ 0x1111u, amp);
    return { h, /*surface*/ 1 /* dirt */ };
}