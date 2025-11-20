#pragma once
#include "world/biome.hpp"

struct BiomePlain : Biome {
    float base = 40.f;    // sea level-ish
    float amp = 6.f;    // small bumps
    float freq = 0.0025f; // scale
    BiomeSample sample(int x, int z, uint32_t seed) const override;
};