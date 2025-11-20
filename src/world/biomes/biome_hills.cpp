#include "world/biomes/biome_hills.hpp"

static inline float fbm(int x, int z, uint32_t seed, float f, int oct, float p = 0.5f) {
    float a = 1.f, v = 0.f, sum = 0.f;
    int X = x, Z = z;
    for (int i = 0; i < oct; i++) {
        uint32_t h = (uint32_t)(X * 374761393) ^ (uint32_t)(Z * 668265263) ^ (seed + i * 1013);
        h = (h ^ (h >> 13)) * 1274126177u; h ^= (h >> 16);
        float u = (h & 0xFFFF) / 65535.0f;
        v += (u * 2.f - 1.f) * a;
        sum += a;
        X = (int)(X * 2); Z = (int)(Z * 2); a *= p;
    }
    return (sum > 0 ? v / sum : 0.f) * f;
}

BiomeSample BiomeHills::sample(int x, int z, uint32_t seed) const {
    float h = base + fbm((int)(x * freq), (int)(z * freq), seed ^ 0x2222u, amp, 4, 0.55f);
    return { h, /*surface*/ 2 /* grass */ };
}
