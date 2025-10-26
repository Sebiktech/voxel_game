// world.hpp
#pragma once
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include "chunk.hpp"
#include "world_gen2.hpp"
#include "mesher.hpp"
#include "vk_utils.hpp"
#include "render_stats.hpp"

struct ChunkGPU {
    VkBuffer vbo = VK_NULL_HANDLE, ibo = VK_NULL_HANDLE;
    VkDeviceMemory vmem = VK_NULL_HANDLE, imem = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t faceCount = 0;
    glm::ivec3 coord{ 0 };

};

struct WorldChunk {
    Chunk data;
    MeshData meshCPU;   // concatenated region mesh (reuse your path)
    ChunkGPU gpu;
    bool    needsUpload = false;
};

struct WorldKey {
    int cx, cy, cz;
    bool operator==(const WorldKey& o) const { return cx == o.cx && cy == o.cy && cz == o.cz; }
};
struct WorldKeyHash {
    size_t operator()(const WorldKey& k) const {
        uint64_t x = (uint32_t)k.cx, y = (uint32_t)k.cy, z = (uint32_t)k.cz;
        return (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
    }
};

struct World {
    std::unordered_map<WorldKey, std::unique_ptr<WorldChunk>, WorldKeyHash> map;
    uint32_t seed = 1337;

    void clearAllChunks();
    WorldChunk* createChunk(const WorldKey& k);
    // ensure chunks in radius (cx,cz), only cy=0 for now
    void ensure(VulkanContext& ctx, int centerCx, int centerCz, int radius);
    void draw(VulkanContext& ctx, VkCommandBuffer cb);
    void destroyGPU(VulkanContext& ctx);
};

// Add declarations (after World struct or near it)
BlockID worldGetBlock(const World& w, int vx, int vy, int vz);
inline bool worldVoxelSolid(const World& w, int vx, int vy, int vz) {
    return worldGetBlock(w, vx, vy, vz) != 0;
}

// Upload any chunks that have needsUpload=true (call once per frame after edits)
void worldUploadDirty(World& w, VulkanContext& ctx);