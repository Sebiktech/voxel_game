#pragma once
#include "world/biome.hpp"

struct BiomeForest : Biome {
    float base = 48.f;
    float amp = 10.f;
    float freq = 0.0020f;
    BiomeSample sample(int x, int z, uint32_t seed) const override;
};
