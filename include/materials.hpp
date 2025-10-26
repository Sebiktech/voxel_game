#pragma once
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "world/world_config.hpp"

// GPU layout-friendly 16-byte multiples
struct Material {
    glm::vec4 tint_emissive; // rgb = tint mul, a = emissive intensity (0..1)
    glm::vec4 extra;         // reserved (roughness, metallic, flags...)
};

// Build a default table sized to ATLAS_N*ATLAS_N (index = tileY*ATLAS_N + tileX)
inline std::vector<Material> buildDefaultMaterials() {
    std::vector<Material> mats(MAX_MATERIALS);
    auto set = [&](int idx, glm::vec3 tint, float emissive) {
        mats[idx].tint_emissive = glm::vec4(tint, emissive);
        mats[idx].extra = glm::vec4(0.8f, 0.0f, 0.0f, 0.0f); // roughness placeholder
        };

    // fill everything with neutral
    for (int i = 0; i < MAX_MATERIALS; i++) set(i, { 1.0f,1.0f,1.0f }, 0.0f);

    // map your used tiles to looks (example)
    // tile (x=0,y=0) -> DEFAULT black
    set(0, { 0.02f,0.02f,0.02f }, 0.0f);
    // tile (1,0) -> DIRT brown
    set(1, { 0.45f,0.28f,0.16f }, 0.0f);
    // tile (2,0) -> GRASS green
    set(2, { 0.35f,0.55f,0.20f }, 0.0f);
    // tile (3,0) -> EMISSIVE test (dim blue)
    set(3, { 0.2f,0.4f,1.0f }, 0.3f);

    return mats;
}
