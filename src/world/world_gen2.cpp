// world_gen2.cpp
#include "world/world_gen2.hpp"
#include "world/biome_map.hpp"
#include "world/world_config.hpp"
#include <algorithm>
#include <cmath>

// ======= Tweakables =======
static constexpr int   SEA_LEVEL = 42;   // water line (world Y)
static constexpr float MOUNTAIN_AMT = 38.f; // max extra height from mountains
static constexpr float HILL_AMT = 10.f; // extra undulation everywhere
static constexpr float VALLEY_AMT = 6.f; // subtractive “valleys”
static constexpr float RIVER_WIDTH = 0.06f;// lower -> wider rivers
static constexpr float CLIFF_SLOPE = 1.7f; // slope threshold for exposed rock

// ======= Noise helpers (fast integer hash value noise) =======
static inline uint32_t hash2i(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 1442695040888963407ull;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static inline float vnoise(int x, int z, uint32_t seed) { // [-1,1]
    return ((hash2i(x, z, seed) & 0xFFFF) / 65535.0f) * 2.f - 1.f;
}
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float smooth(float t) { return t * t * (3.f - 2.f * t); }

// value noise, bilinear-smoothed at float coords
static float value2D(float fx, float fz, float scale, uint32_t seed) {
    float x = fx * scale, z = fz * scale;
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float tx = x - xi, tz = z - zi;
    float v00 = vnoise(xi, zi, seed);
    float v10 = vnoise(xi + 1, zi, seed);
    float v01 = vnoise(xi, zi + 1, seed);
    float v11 = vnoise(xi + 1, zi + 1, seed);
    float vx0 = lerp(v00, v10, smooth(tx));
    float vx1 = lerp(v01, v11, smooth(tx));
    return lerp(vx0, vx1, smooth(tz));
}

static float fbm(float fx, float fz, uint32_t seed, int oct, float baseFreq, float gain = 0.5f, float lac = 2.0f) {
    float amp = 1.f, freq = baseFreq, sum = 0.f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        sum += value2D(fx, fz, freq, seed + i * 1013u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return (norm > 0 ? sum / norm : 0.f); // [-1,1]
}

// Ridged multifractal
static float ridged(float fx, float fz, uint32_t seed, int oct, float baseFreq, float gain = 0.5f, float lac = 2.0f) {
    float amp = 1.f, freq = baseFreq, sum = 0.f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        float n = 1.f - std::fabs(value2D(fx, fz, freq, seed + i * 733u)); // [0,1] ridges
        n *= n;
        sum += n * amp;
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return (norm > 0 ? sum / norm : 0.f); // [0,1]
}

// River mask: near zero of a low-frequency value noise
static float riverMask(float fx, float fz, uint32_t seed, float baseFreq) {
    float n = value2D(fx, fz, baseFreq, seed ^ 0xA1A1u); // [-1,1]
    return std::exp(-(n * n) / (RIVER_WIDTH * RIVER_WIDTH)); // ~[0,1], wide near 0
}

// Slope estimate via central differences of heightfield
static float slopeAt(const BiomeMap& BM, int wx, int wz, uint32_t seed) {
    // sample blended height (we don’t need perfect; 1-voxel offsets)
    float hC = BM.blended(wx, wz, seed).height;
    float hX = BM.blended(wx + 1, wz, seed).height - BM.blended(wx - 1, wz, seed).height;
    float hZ = BM.blended(wx, wz + 1, seed).height - BM.blended(wx, wz - 1, seed).height;
    // magnitude (bigger = steeper)
    return std::sqrt((hX * hX + hZ * hZ) * 0.25f);
}

// ======= Biome map instance =======
static BiomeMap BIOMES;

// ======= Main generation =======
void generateChunk(Chunk& c, ChunkCoord cc, uint32_t seed)
{
    const int wx0 = cc.cx * CHUNK_SIZE;
    const int wz0 = cc.cz * CHUNK_SIZE;
    const int wy0 = cc.cy * CHUNK_HEIGHT;

    // Precompute a gentle “continent mask” to push oceans down near edges of big blobs
    // (Very low frequency so we get large landmasses)
    auto continent = [&](int wx, int wz) {
        float f = fbm((float)wx, (float)wz, seed ^ 0xC001u, 5, 0.0003f, 0.55f, 2.1f); // [-1,1]
        return (f + 1.f) * 0.5f; // [0,1]
        };

    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            const int wx = wx0 + x;
            const int wz = wz0 + z;

            // Base from your blended biomes (smooth transitions)
            BiomeSample bs = BIOMES.blended(wx, wz, seed);
            float baseH = bs.height;

            // Mountains (ridged) modulated by a mid-low frequency mask
            float mMask = std::clamp(fbm((float)wx, (float)wz, seed ^ 0x55AAu, 3, 0.0012f, 0.6f, 2.1f) * 0.5f + 0.5f, 0.f, 1.f);
            float mountains = ridged((float)wx, (float)wz, seed ^ 0x1337u, 5, 0.0009f, 0.5f, 2.0f) * MOUNTAIN_AMT * mMask;

            // Hills everywhere, mild
            float hills = fbm((float)wx, (float)wz, seed ^ 0x7777u, 4, 0.0020f, 0.5f, 2.0f) * HILL_AMT;

            // Valleys subtract height slightly for variation
            float valleys = std::abs(fbm((float)wx, (float)wz, seed ^ 0x4242u, 3, 0.0016f, 0.55f, 2.0f)) * VALLEY_AMT;

            // Rivers carve near zero of a very low frequency noise
            float rMask = riverMask((float)wx, (float)wz, seed, 0.0007f);
            float riverCut = rMask * 12.f; // depth of riverbeds

            // Continents push ocean down in some regions
            float cont = continent(wx, wz);
            float oceanPush = (cont < 0.45f) ? ((0.45f - cont) * 24.f) : 0.f;

            // Compose final height
            float H = baseH + hills + mountains - valleys - riverCut - oceanPush;

            // Slope for cliff material logic
            float slope = slopeAt(BIOMES, wx, wz, seed);

            // Decide surface material by elevation & slope
            uint16_t surface = bs.surfaceId; // start from biome suggestion
            if (H < SEA_LEVEL + 1) {
                surface = BLOCK_SAND; // beaches and riverbanks
            }
            else if (slope > CLIFF_SLOPE) {
                surface = BLOCK_STONE; // exposed cliffs
            }
            else if (H > SEA_LEVEL + 30) {
                surface = BLOCK_SNOW;  // high elevation
            }
            else {
                surface = BLOCK_GRASS; // default mid elevation
            }

            // Fill vertical column
            // choose a “stone depth” for mountains (deeper rock) and dirt thickness elsewhere
            int groundY = (int)std::floor(H);
            int stoneDepth = (int)std::clamp(4 + mMask * 6, 4.f, 12.f);

            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                const int wy = wy0 + y;
                uint16_t id = BLOCK_AIR;

                // Ocean / rivers
                if (wy <= SEA_LEVEL && wy > groundY) {
                    id = BLOCK_WATER;
                }
                else if (wy <= groundY - stoneDepth) {
                    id = BLOCK_STONE;            // deep rock
                }
                else if (wy < groundY) {
                    id = BLOCK_DIRT;             // soil
                }
                else if (wy == groundY) {
                    id = surface;                // top surface based on rules above
                }

                c.set(x, y, z, id);
            }
        }
}
