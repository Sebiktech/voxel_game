#pragma once
#include <glm/glm.hpp>
#include <array>
#include "noise.hpp"

enum BiomeId { BIOME_PLAINS, BIOME_FOREST, BIOME_DESERT, BIOME_MOUNTAINS, BIOME_COUNT };

struct Biome {
    float base;     // base height (in voxels, smallest grid)
    float amp;      // variation amplitude
    int   topId;    // surface block
    int   fillId;   // under-surface fill
    float rough;    // noise frequency scale
};

inline const std::array<Biome, BIOME_COUNT>& biomeTable() {
    static std::array<Biome, BIOME_COUNT> B{
        Biome{  96.f,  16.f, /*top*/3, /*fill*/2,  0.005f }, // PLAINS (grass over dirt)
        Biome{ 112.f,  28.f, /*top*/3, /*fill*/2,  0.006f }, // FOREST
        Biome{  88.f,  12.f, /*top*/1, /*fill*/1,  0.008f }, // DESERT (all default/sand-ish)
        Biome{ 160.f,  80.f, /*top*/1, /*fill*/1,  0.004f }, // MOUNTAINS (stone)
    };
    return B;
}

// Soft biome weights (sum to 1). world x,z are in smallest-world units.
inline glm::vec4 biomeWeights(float wx, float wz, uint32_t seed) {
    float s = 1.0f / 512.0f;
    float p = fbm2(wx * s, wz * s, seed * 11u, 4, 2.0f, 0.5f);
    float m = fbm2(wx * (s * 0.6f), wz * (s * 0.6f), seed * 23u, 4, 2.0f, 0.5f);
    float d = fbm2(wx * (s * 1.8f), wz * (s * 1.8f), seed * 37u, 4, 2.0f, 0.5f);
    float f = fbm2(wx * (s * 0.9f), wz * (s * 0.9f), seed * 41u, 4, 2.0f, 0.5f);

    // logits ? softmax
    glm::vec4 logits{ p * 1.2f, f * 1.1f, d * 1.3f, m * 1.8f };
    glm::vec4 e = glm::exp(logits - glm::vec4(glm::max(glm::max(logits.x, logits.y), glm::max(logits.z, logits.w))));
    return e / glm::dot(e, glm::vec4(1));
}