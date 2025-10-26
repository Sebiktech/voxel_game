// world_gen2.cpp
#include "world/world_gen2.hpp"

void generateChunk(Chunk& c, ChunkCoord cc, uint32_t seed)
{
    // world-space origin of this chunk in smallest cells
    const int wx0 = cc.cx * CHUNK_SIZE;
    const int wz0 = cc.cz * CHUNK_SIZE;
    const int wy0 = cc.cy * CHUNK_HEIGHT;

    const auto& B = biomeTable();

    // column-wise heightmap per x,z
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            float wx = float(wx0 + x);
            float wz = float(wz0 + z);

            glm::vec4 w = biomeWeights(wx * VOXEL_SCALE, wz * VOXEL_SCALE, seed); // world units
            // blend biome heights
            float hPln = B[BIOME_PLAINS].base + B[BIOME_PLAINS].amp * fbm2(wx * B[BIOME_PLAINS].rough, wz * B[BIOME_PLAINS].rough, seed * 101u);
            float hFor = B[BIOME_FOREST].base + B[BIOME_FOREST].amp * fbm2(wx * B[BIOME_FOREST].rough, wz * B[BIOME_FOREST].rough, seed * 103u);
            float hDes = B[BIOME_DESERT].base + B[BIOME_DESERT].amp * fbm2(wx * B[BIOME_DESERT].rough, wz * B[BIOME_DESERT].rough, seed * 107u);
            float hMou = B[BIOME_MOUNTAINS].base + B[BIOME_MOUNTAINS].amp * fbm2(wx * B[BIOME_MOUNTAINS].rough, wz * B[BIOME_MOUNTAINS].rough, seed * 109u);
            float hBlend = hPln * w.x + hFor * w.y + hDes * w.z + hMou * w.w;

            // convert to smallest voxels within this vertical chunk
            int worldH = clampi(int(std::round(hBlend)), 0, 100000);
            int yTop = worldH - wy0; // relative to this chunk's base

            // choose dominant biome for surface IDs
            int biomeIdx = 0; float best = w.x;
            if (w.y > best) { biomeIdx = 1; best = w.y; }
            if (w.z > best) { biomeIdx = 2; best = w.z; }
            if (w.w > best) { biomeIdx = 3; best = w.w; }

            int topId = B[biomeIdx].topId;
            int fillId = B[biomeIdx].fillId;

            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                int wy = wy0 + y;
                int id = 0;
                if (wy <= worldH) {
                    // top 1–2 voxels = topId, below = fillId
                    id = (wy >= worldH - 1) ? topId : fillId;
                }
                c.set(x, y, z, (BlockID)id);
            }

            // simple caves (optional)
            float cave = fbm2(wx * 0.03f, wz * 0.03f, seed * 1009u);
            int caveY = int(80 + cave * 40);
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                int wy = wy0 + y;
                if (wy < caveY && c.get(x, y, z) != 0) {
                    float cav = fbm2((wx + wy) * 0.02f, (wz - wy) * 0.02f, seed * 2003u);
                    if (cav > 0.65f) c.set(x, y, z, 0);
                }
            }
        }
    }
}