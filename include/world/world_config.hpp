#pragma once
using BlockID = uint16_t;

// 0.5x voxel => na rovnakú fyzickú výšku treba 2× viac blokov
constexpr float VOXEL_SCALE = 0.25f;
constexpr int   VOXEL_HEIGHT_SCALE = 4;   // = int(1.0f / VOXEL_SCALE)

// ve?ký blok = 2×2×2 malých
constexpr int BIG_BLOCK_SIZE = 2;

// Atlas tiles per row/column (you already used 4x4 earlier)
constexpr int ATLAS_N = 4;

// Max materials in UBO (N*N tiles by default)
constexpr int MAX_MATERIALS = ATLAS_N * ATLAS_N;

// Block IDs you use now (extend as you grow)
enum BlockId : uint16_t {
    BLOCK_AIR = 0,
    BLOCK_DEFAULT = 1,   // your default black
    BLOCK_DIRT = 2,
    BLOCK_GRASS = 3,
    // ... add more as you map tiles
}; 