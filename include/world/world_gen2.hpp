// world_gen2.hpp
#pragma once
#include "chunk.hpp"
#include "biome.hpp"
#include "world_config.hpp"
#include <glm/glm.hpp>

struct ChunkCoord { int cx, cy, cz; };

void generateChunk(Chunk& c, ChunkCoord cc, uint32_t seed);