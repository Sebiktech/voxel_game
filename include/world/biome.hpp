#pragma once
#include <cstdint>   // <-- needed for uint16_t, uint32_t

struct BiomeSample {
    float height;        // ground height (world Y) at (x,z)
    uint16_t surfaceId;  // material/block id for top block
};

struct Biome {
    virtual ~Biome() = default;
    virtual BiomeSample sample(int x, int z, uint32_t seed) const = 0;
};