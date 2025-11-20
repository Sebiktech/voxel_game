#pragma once
#include "world/biome.hpp"

struct BiomeHills : Biome {
    float base = 54.f;
    float amp = 18.f;
    float freq = 0.0018f;
    BiomeSample sample(int x, int z, uint32_t seed) const override;
};