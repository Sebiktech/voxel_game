#pragma once
#include <cstdint>

struct RenderStats {
    uint64_t drawVertices = 0;
    uint64_t drawIndices = 0;
    uint64_t drawTriangles = 0;
    uint64_t drawFaces = 0;
};