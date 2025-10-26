#pragma once
#include "chunk.hpp"

// Greedy mesher (rovnaký názov, iná implementácia)
MeshData meshChunk(const Chunk& c);

// New: build mesh with a world-space offset from chunk coords (cx,cy,cz)
MeshData meshChunkAt(const Chunk& c, int cx, int cy, int cz);

MeshData meshChunkRegion(const Chunk& c, int x0, int y0, int z0, int x1, int y1, int z1);