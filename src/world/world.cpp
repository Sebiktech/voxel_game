// world.cpp
#include "world/world.hpp"
#include <vector>
#include <iostream>

static void destroyChunkGPU(VkDevice dev, ChunkGPU& g) {
    if (g.vbo) { vkDestroyBuffer(dev, g.vbo, nullptr); g.vbo = VK_NULL_HANDLE; }
    if (g.vmem) { vkFreeMemory(dev, g.vmem, nullptr); g.vmem = VK_NULL_HANDLE; }
    if (g.ibo) { vkDestroyBuffer(dev, g.ibo, nullptr); g.ibo = VK_NULL_HANDLE; }
    if (g.imem) { vkFreeMemory(dev, g.imem, nullptr); g.imem = VK_NULL_HANDLE; }
    g.indexCount = 0;
}

static bool createAndFill(VulkanContext& ctx, const void* data, VkDeviceSize bytes,
    VkBufferUsageFlags usage, VkBuffer& outB, VkDeviceMemory& outM);

void World::ensure(VulkanContext& ctx, int centerCx, int centerCz, int radius)
{
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) {
            WorldKey k{ centerCx + dx, 0, centerCz + dz };
            if (map.find(k) != map.end()) continue;

            auto wc = std::make_unique<WorldChunk>();
            // generate
            generateChunk(wc->data, { k.cx,k.cy,k.cz }, seed);

            // mesh whole chunk once (reuse your concatenation path)
            MeshData m = meshChunkAt(wc->data, k.cx, k.cy, k.cz);
            wc->meshCPU = std::move(m);
            wc->needsUpload = true;

            map.emplace(k, std::move(wc));
        }

    // upload any new/dirty chunks
    for (auto& kv : map) {
        auto& wc = *kv.second;
        if (!wc.needsUpload) continue;
        wc.needsUpload = false;

        // upload VBO/IBO
        destroyChunkGPU(ctx.device, wc.gpu);
        if (!wc.meshCPU.vertices.empty()) {
            createAndFill(ctx, wc.meshCPU.vertices.data(),
                VkDeviceSize(wc.meshCPU.vertices.size() * sizeof(float)),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                wc.gpu.vbo, wc.gpu.vmem);
        }
        if (!wc.meshCPU.indices.empty()) {
            createAndFill(ctx, wc.meshCPU.indices.data(),
                VkDeviceSize(wc.meshCPU.indices.size() * sizeof(uint32_t)),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                wc.gpu.ibo, wc.gpu.imem);
        }
        wc.gpu.indexCount = (uint32_t)wc.meshCPU.indices.size();
        wc.gpu.vertexCount = (uint32_t)(wc.meshCPU.vertices.size() / 10);
        wc.gpu.faceCount = wc.gpu.indexCount / 6;
        wc.gpu.coord = { kv.first.cx, kv.first.cy, kv.first.cz };
    }
}

void World::draw(VulkanContext& ctx, VkCommandBuffer cb)
{
    for (auto& kv : map) {
        const auto& g = kv.second->gpu;
        if (!g.vbo || !g.ibo || g.indexCount == 0) continue;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &g.vbo, &off);
        vkCmdBindIndexBuffer(cb, g.ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, g.indexCount, 1, 0, 0, 0);
    }
}

void World::destroyGPU(VulkanContext& ctx) {
    for (auto& kv : map) destroyChunkGPU(ctx.device, kv.second->gpu);
}

// ——— minimal staging uploader (uses your createBuffer/copyBuffer)
static bool createAndFill(VulkanContext& ctx, const void* data, VkDeviceSize bytes,
    VkBufferUsageFlags usage, VkBuffer& outB, VkDeviceMemory& outM)
{
    if (bytes == 0) { outB = VK_NULL_HANDLE; outM = VK_NULL_HANDLE; return true; }
    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory smem = VK_NULL_HANDLE;
    if (!createBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging, smem))
        return false;
    void* mapped = nullptr; vkMapMemory(ctx.device, smem, 0, bytes, 0, &mapped);
    std::memcpy(mapped, data, (size_t)bytes); vkUnmapMemory(ctx.device, smem);

    if (!createBuffer(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outB, outM))
    {
        vkDestroyBuffer(ctx.device, staging, nullptr); vkFreeMemory(ctx.device, smem, nullptr); return false;
    }

    if (!copyBuffer(ctx, staging, outB, bytes))
    {
        vkDestroyBuffer(ctx.device, outB, nullptr); vkFreeMemory(ctx.device, outM, nullptr);
        vkDestroyBuffer(ctx.device, staging, nullptr); vkFreeMemory(ctx.device, smem, nullptr); return false;
    }

    vkDestroyBuffer(ctx.device, staging, nullptr); vkFreeMemory(ctx.device, smem, nullptr);
    return true;
}

static inline int floordiv(int a, int b) {
    int q = a / b;
    int r = a % b;
    return (r && ((r < 0) != (b < 0))) ? (q - 1) : q;
}
static inline int floormod(int a, int b) {
    int m = a % b;
    if (m < 0) m += (b < 0 ? -b : b);
    return m;
}

BlockID worldGetBlock(const World& w, int vx, int vy, int vz) {
    int cx = floordiv(vx, CHUNK_SIZE);
    int cy = floordiv(vy, CHUNK_HEIGHT);
    int cz = floordiv(vz, CHUNK_SIZE);
    int lx = floormod(vx, CHUNK_SIZE);
    int ly = floormod(vy, CHUNK_HEIGHT);
    int lz = floormod(vz, CHUNK_SIZE);

    WorldKey k{ cx, cy, cz };
    auto it = w.map.find(k);
    if (it == w.map.end()) return 0; // not loaded -> treat as empty
    return it->second->data.get(lx, ly, lz);
}

static inline void worldToVoxel(const glm::vec3& w, int& x, int& y, int& z) {
    x = (int)std::floor(w.x / VOXEL_SCALE + 0.5f);
    y = (int)std::floor(w.y / VOXEL_SCALE + 0.5f);
    z = (int)std::floor(w.z / VOXEL_SCALE + 0.5f);
}

static inline bool voxelSolidW(const World& w, int x, int y, int z) {
    return worldVoxelSolid(w, x, y, z);
}

void worldUploadDirty(World& w, VulkanContext& ctx)
{
    for (auto& kv : w.map) {
        WorldChunk& wc = *kv.second;
        if (!wc.needsUpload) continue;
        wc.needsUpload = false;

        // destroy previous GPU buffers
        // (assumes you have this helper in world.cpp)
        destroyChunkGPU(ctx.device, wc.gpu);

        // create + fill VBO
        if (!wc.meshCPU.vertices.empty()) {
            createAndFill(ctx,
                wc.meshCPU.vertices.data(),
                VkDeviceSize(wc.meshCPU.vertices.size() * sizeof(float)),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                wc.gpu.vbo, wc.gpu.vmem);
        }

        // create + fill IBO
        if (!wc.meshCPU.indices.empty()) {
            createAndFill(ctx,
                wc.meshCPU.indices.data(),
                VkDeviceSize(wc.meshCPU.indices.size() * sizeof(uint32_t)),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                wc.gpu.ibo, wc.gpu.imem);
        }

        wc.gpu.indexCount = static_cast<uint32_t>(wc.meshCPU.indices.size());
        // wc.gpu.coord already set when chunk was created
    }
}

void World::clearAllChunks() {
    // If you have GPU buffers in chunks, defer-destroy them here
    map.clear();
}

WorldChunk* World::createChunk(const WorldKey& k) {
    auto it = map.find(k);
    if (it != map.end()) return it->second.get();
    auto wc = std::make_unique<WorldChunk>();
    // init voxel storage (set to AIR)
    for (int y = 0; y < CHUNK_HEIGHT; ++y)
        for (int z = 0; z < CHUNK_SIZE; ++z)
            for (int x = 0; x < CHUNK_SIZE; ++x)
                wc->data.set(x, y, z, BLOCK_AIR);
    auto* ptr = wc.get();
    map.emplace(k, std::move(wc));
    return ptr;
}