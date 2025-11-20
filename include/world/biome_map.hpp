#pragma once
#include <memory>
#include "world/biome.hpp"
#include "world/biomes/biome_plain.hpp"
#include "world/biomes/biome_hills.hpp"
#include "world/biomes/biome_forest.hpp"

struct BiomeMap {
    BiomePlain  plain;
    BiomeForest forest;
    BiomeHills  hills;

    // returns blended height + dominant surfaceId for (x,z)
    BiomeSample blended(int x, int z, uint32_t seed) const; 
};