#include "world/mesher.hpp"
#include "world/world_config.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

static inline bool isAir(BlockID id) { return id == 0; }
static inline bool isSolid(BlockID id) { return id != 0; }

// --- Ambient Occlusion helpers ---
static inline int occ(const Chunk& c, int x, int y, int z) {
    // solid? adapt if your "air" id differs
    return c.get(x, y, z) != BLOCK_AIR;
}

// Bounds-safe occupancy for AO
static inline int occSafe(const Chunk& c, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 ||
        x >= CHUNK_SIZE || y >= CHUNK_HEIGHT || z >= CHUNK_SIZE) return 0;
    return c.get(x, y, z) != BLOCK_AIR;
}

// Map 0..3 occluders ? AO factor (tweak to taste)
static inline float aoFactor(int side1, int side2, int corner) {
    const int n = side1 + side2 + corner;
    switch (n) {
    case 0: return 1.00f;
    case 1: return 0.80f;
    case 2: return 0.60f;
    default:return 0.45f;
    }
}

static inline void tileToUV(float tx, float ty, float& u, float& v) {
    u = tx / ATLAS_N; v = ty / ATLAS_N;
}

// id in [1..MAX_MATERIALS-1] maps to (tx,ty) in [0..ATLAS_N-1]
static inline void tileFromId(BlockID id, int& tx, int& ty) {
    if (id == 0) { tx = ty = 0; return; }
    int idx = int(id - 1);
    tx = idx % ATLAS_N;
    ty = idx / ATLAS_N;
    // clamp just in case
    if (tx < 0) tx = 0; if (tx >= ATLAS_N) tx = ATLAS_N - 1;
    if (ty < 0) ty = 0; if (ty >= ATLAS_N) ty = ATLAS_N - 1;
}

// your existing pickTile can now be simplified to just compute vTile from id
static inline void pickTile(BlockID id, int /*faceDir*/, int /*axis*/, float& tileU, float& tileV)
{
    int tx, ty;
    tileFromId(id, tx, ty);
    tileU = tx * (1.0f / float(ATLAS_N));
    tileV = ty * (1.0f / float(ATLAS_N));
}

// Vypíše jeden ve?ký obd?žnik (du x dv voxelov) na „hranici“ slice-u k.
// Pozície sedia s tvojím starým +/-0.5 layoutom.
static inline void emitQuad(class MeshData& m, const Chunk& chunk,
    int axis, int faceDir,        // 0=x,1=y,2=z ; +1/-1
    int k,                        // slice index (between k-1 and k)
    int i0, int j0,               // start in plane (u,v)
    int du, int dv,               // width/height in voxels
    float tileU, float tileV)
{
    static constexpr float VOXEL_SCALE = 0.25f;

    // In-plane axes
    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;

    // Index of the "solid" layer along the slicing axis (see how faceDir is chosen in your mask)
    const int solidLayer = (faceDir > 0) ? (k - 1) : (k);

    // Helper to translate (iu,iv,axisLayer) into (x,y,z)
    auto pack = [&](int iu, int iv, int axVal, int& X, int& Y, int& Z) {
        int a[3]; a[axis] = axVal; a[u] = iu; a[v] = iv; X = a[0]; Y = a[1]; Z = a[2];
        };

    // AO for a corner: look at two orthogonal neighbors + the diagonal on the SOLID side
    auto cornerAO = [&](int iu0, int iv0, int duSign, int dvSign)->float {
        int Xs, Ys, Zs;
        int s1x, s1y, s1z; // neighbor along +/?u
        int s2x, s2y, s2z; // neighbor along +/?v
        int crx, cry, crz; // diagonal (+/?u, +/?v)

        pack(iu0, iv0, solidLayer, Xs, Ys, Zs);
        pack(iu0 + duSign, iv0, solidLayer, s1x, s1y, s1z);
        pack(iu0, iv0 + dvSign, solidLayer, s2x, s2y, s2z);
        pack(iu0 + duSign, iv0 + dvSign, solidLayer, crx, cry, crz);

        int s1 = occSafe(chunk, s1x, s1y, s1z);
        int s2 = occSafe(chunk, s2x, s2y, s2z);
        int cr = occSafe(chunk, crx, cry, crz);
        // Map 0..3 occluders ? AO factor (reuse your aoFactor)
        return aoFactor(s1, s2, cr);
        };

    // AO for the 4 quad corners (match the same corner order as ij[])
    // ij[] = { {0,0}, {0,dv}, {du,dv}, {du,0} }
    float ao00 = cornerAO(i0, j0, -1, -1); // (0,0)   bottom-left
    float ao0V = cornerAO(i0, j0 + dv - 1, -1, +1); // (0,dv)  top-left
    float aoUV = cornerAO(i0 + du - 1, j0 + dv - 1, +1, +1); // (du,dv) top-right
    float aoU0 = cornerAO(i0 + du - 1, j0, +1, -1); // (du,0)  bottom-right

    // Map those AO values to the vertex emit order
    float aoCorner[4] = { ao00, ao0V, aoUV, aoU0 };

    // Fixed plane coordinate at the face
    const float plane = ((float)k - 0.5f) * VOXEL_SCALE;

    // Normal
    float nx = 0, ny = 0, nz = 0;
    if (axis == 0) nx = (float)faceDir;
    else if (axis == 1) ny = (float)faceDir;
    else nz = (float)faceDir;

    // corner offsets
    int ij[4][2] = { {0,0},{0,dv},{du,dv},{du,0} };

    // 4 vertices: pos3 normal3 uv2 tile2 + AO(1) => 11 floats
    uint32_t base = static_cast<uint32_t>(m.vertices.size() / 11);
    for (int idx = 0; idx < 4; ++idx) {
        int offU = ij[idx][0];
        int offV = ij[idx][1];

        float pos[3];
        pos[axis] = plane;
        pos[u] = ((float)(i0 + offU) - 0.5f) * VOXEL_SCALE;
        pos[v] = ((float)(j0 + offV) - 0.5f) * VOXEL_SCALE;

        // pos
        m.vertices.push_back(pos[0]);
        m.vertices.push_back(pos[1]);
        m.vertices.push_back(pos[2]);
        // normal
        m.vertices.push_back(nx);
        m.vertices.push_back(ny);
        m.vertices.push_back(nz);
        // uv in voxel units (0..du, 0..dv)
        m.vertices.push_back((float)offU);
        m.vertices.push_back((float)offV);
        // tile offset (atlas cell)
        m.vertices.push_back(tileU);
        m.vertices.push_back(tileV);
        // NEW: AO
        m.vertices.push_back(aoCorner[idx]);
    }

    // indices (same winding you already had)
    if (faceDir > 0) {
        m.indices.push_back(base + 0); m.indices.push_back(base + 1); m.indices.push_back(base + 2);
        m.indices.push_back(base + 0); m.indices.push_back(base + 2); m.indices.push_back(base + 3);
    }
    else {
        m.indices.push_back(base + 0); m.indices.push_back(base + 2); m.indices.push_back(base + 1);
        m.indices.push_back(base + 0); m.indices.push_back(base + 3); m.indices.push_back(base + 2);
    }
}

struct MaskCell {
    BlockID id{};
    int8_t  faceDir{}; // +1 alebo -1 (0 = ni?)
};

static inline bool sameCell(const MaskCell& a, const MaskCell& b) {
    return a.id == b.id && a.faceDir == b.faceDir;
}

// Greedy mesher – nahrádza pôvodný meshChunk
MeshData meshChunk(const Chunk& c) {
    MeshData out;
    out.vertices.clear(); out.indices.clear();

    const int dims[3] = { CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE };

    // Pre každý axis vykreslíme pláty medzi slice-ami k-1 a k
    for (int axis = 0; axis < 3; ++axis) {
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        int du = dims[u], dv = dims[v], dw = dims[axis];

        std::vector<MaskCell> mask(du * dv);

        for (int k = 0; k <= dw; ++k) {
            // Vypo?ítaj masku tvárí medzi k-1 a k
            for (int j = 0; j < dv; ++j) {
                for (int i = 0; i < du; ++i) {
                    int a[3] = { 0,0,0 }, b[3] = { 0,0,0 };
                    a[axis] = k - 1; b[axis] = k;
                    a[u] = i; a[v] = j; b[u] = i; b[v] = j;

                    BlockID va = (k > 0 ? c.get((axis == 0 ? a[0] : a[0]), (axis == 1 ? a[1] : a[1]), (axis == 2 ? a[2] : a[2])) : 0);
                    BlockID vb = (k < dw ? c.get((axis == 0 ? b[0] : b[0]), (axis == 1 ? b[1] : b[1]), (axis == 2 ? b[2] : b[2])) : 0);

                    MaskCell cell{};
                    if (isSolid(va) != isSolid(vb)) {
                        // Normála smerom od SOLID do AIR => ak je va solid, je to +face; inak -face.
                        cell.id = isSolid(va) ? va : vb;
                        cell.faceDir = isSolid(va) ? +1 : -1;
                    }
                    mask[j * du + i] = cell;
                }
            }

            // Greedy zlú?enie masky do obd?žnikov
            int i = 0, j = 0;
            while (j < dv) {
                while (i < du) {
                    MaskCell m0 = mask[j * du + i];
                    if (m0.faceDir == 0) { ++i; continue; }

                    // šírka
                    int w = 1;
                    while (i + w < du && sameCell(mask[j * du + i + w], m0)) ++w;

                    // výška
                    int h = 1;
                    bool stop = false;
                    while (j + h < dv && !stop) {
                        for (int x = 0; x < w; ++x) {
                            if (!sameCell(mask[(j + h) * du + i + x], m0)) { stop = true; break; }
                        }
                        if (!stop) ++h;
                    }

                    // emitni quad (i,j) .. (i+w,j+h) na slice k
                    float tileU, tileV;
                    pickTile(m0.id, m0.faceDir, axis, tileU, tileV);
                    emitQuad(out, c, axis, m0.faceDir, k, i, j, w, h, tileU, tileV);

                    // vy?isti použitú oblas? v maske
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                            mask[(j + y) * du + (i + x)] = MaskCell{};

                    i += w;
                }
                i = 0; ++j;
            }
        }
    }

    return out;
}

MeshData meshChunkAt(const Chunk& c, int cx, int cy, int cz)
{
    MeshData m = meshChunk(c);

    const float xOff = float(cx * CHUNK_SIZE) * VOXEL_SCALE;
    const float yOff = float(cy * CHUNK_HEIGHT) * VOXEL_SCALE;
    const float zOff = float(cz * CHUNK_SIZE) * VOXEL_SCALE;

    // add offset to every vertex position
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 11) {
        m.vertices[i + 0] += xOff; // x
        m.vertices[i + 1] += yOff; // y
        m.vertices[i + 2] += zOff; // z
    }
    return m;
}

// === BOUNDED GREEDY MESHER FOR A SUB-REGION ===
// x0,y0,z0 inclusive  |  x1,y1,z1 exclusive  (all in smallest-cell coords)
MeshData meshChunkRegion(const Chunk& c, int x0, int y0, int z0, int x1, int y1, int z1)
{
    MeshData out;
    out.vertices.clear(); out.indices.clear();

    const int dims[3] = { CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE };

    auto inBounds = [&](int x, int y, int z)->bool {
        return (x >= 0 && y >= 0 && z >= 0 && x < dims[0] && y < dims[1] && z < dims[2]);
        };
    auto getSafe = [&](int x, int y, int z)->BlockID {
        return inBounds(x, y, z) ? c.get(x, y, z) : 0;
        };

    // For each axis we build faces between slices k-1 and k.
    for (int axis = 0; axis < 3; ++axis) {
        // map axis->(u,v) in-plane axes and region bounds per axis
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;

        int lo[3] = { x0,y0,z0 };
        int hi[3] = { x1,y1,z1 };

        // in-plane extents for this axis
        int u0 = lo[u], u1 = hi[u];
        int v0 = lo[v], v1 = hi[v];
        int du = std::max(0, u1 - u0);
        int dv = std::max(0, v1 - v0);
        if (du == 0 || dv == 0) continue;

        // along the slicing axis we need k in [lo[axis] .. hi[axis]]
        // because faces lie between k-1 and k; both ends are needed.
        int k0 = lo[axis];
        int k1 = hi[axis];

        std::vector<MaskCell> mask(du * dv);

        for (int k = k0; k <= k1; ++k) {
            // build mask for this slice
            for (int j = 0; j < dv; ++j) {
                for (int i = 0; i < du; ++i) {
                    int iu = u0 + i;
                    int iv = v0 + j;

                    // samples on either side of the plane between k-1 and k
                    int ax[3] = { 0,0,0 }, bx[3] = { 0,0,0 };
                    ax[u] = iu; ax[v] = iv; ax[axis] = k - 1;
                    bx[u] = iu; bx[v] = iv; bx[axis] = k;

                    BlockID va = (k > 0 ? getSafe(ax[0], ax[1], ax[2]) : 0);
                    BlockID vb = (k < dims[axis] ? getSafe(bx[0], bx[1], bx[2]) : 0);

                    MaskCell cell{}; // default empty
                    if (isSolid(va) != isSolid(vb)) {
                        cell.id = isSolid(va) ? va : vb;
                        cell.faceDir = isSolid(va) ? +1 : -1;
                    }
                    mask[j * du + i] = cell;
                }
            }

            // greedy merge on mask
            int i = 0, j = 0;
            while (j < dv) {
                while (i < du) {
                    MaskCell m0 = mask[j * du + i];
                    if (m0.faceDir == 0) { ++i; continue; }

                    // width
                    int w = 1;
                    while (i + w < du && sameCell(mask[j * du + i + w], m0)) ++w;
                    // height
                    int h = 1; bool stop = false;
                    while (j + h < dv && !stop) {
                        for (int x = 0; x < w; ++x) {
                            if (!sameCell(mask[(j + h) * du + i + x], m0)) { stop = true; break; }
                        }
                        if (!stop) ++h;
                    }

                    // emit quad using ABSOLUTE in-plane coordinates
                    float tileU, tileV;
                    pickTile(m0.id, m0.faceDir, axis, tileU, tileV);

                    int i0_abs = u0 + i;
                    int j0_abs = v0 + j;
                    emitQuad(out, c, axis, m0.faceDir, k, i0_abs, j0_abs, w, h, tileU, tileV);

                    // clear mask block
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                            mask[(j + y) * du + (i + x)] = MaskCell{};

                    i += w;
                }
                i = 0; ++j;
            }
        }
    }

    return out;
}