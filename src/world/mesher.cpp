#include "world/mesher.hpp"
#include "world/world_config.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

static inline bool isAir(BlockID id) { return id == 0; }
static inline bool isSolid(BlockID id) { return id != 0; }

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
static inline void emitQuad(class MeshData& m,
    int axis, int faceDir,        // 0=x,1=y,2=z ; +1/-1
    int k,                        // slice index (medzi k-1 a k)
    int i0, int j0,               // za?iatok v rovine (u,v)
    int du, int dv,               // šírka/výška v voxeloch
    float tileU, float tileV)
{
    static constexpr float VOXEL_SCALE = 0.25f;
    constexpr int   VOXEL_HEIGHT_SCALE = 2;   // = int(1.0f / VOXEL_SCALE)

    // osi v rovine
    int u = (axis + 1) % 3;
    int v = (axis + 2) % 3;

    // pomocné pre rohy (0..du, 0..dv)
    int ij[4][2] = { {0,0},{0,dv},{du,dv},{du,0} };

    // pevná súradnica roviny (vždy k - 0.5 na hranici medzi k-1 a k)
    const float plane = ((float)k - 0.5f) * VOXEL_SCALE;

    // normála
    float nx = 0, ny = 0, nz = 0;
    if (axis == 0) nx = (float)faceDir;
    else if (axis == 1) ny = (float)faceDir;
    else nz = (float)faceDir;

    // 4 vrcholy: pos3 normal3 uv2 tile2
    uint32_t base = static_cast<uint32_t>(m.vertices.size() / 10);
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
        // uv: v „voxel“ jednotkách -> 0..du, 0..dv (opakujeme textúru)
        m.vertices.push_back((float)offU);
        m.vertices.push_back((float)offV);
        // tile offset
        m.vertices.push_back(tileU);
        m.vertices.push_back(tileV);
    }

    // Winding: pre +faces CCW (0,1,2, 0,2,3), pre -faces prehodíme
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
                    emitQuad(out, axis, m0.faceDir, k, i, j, w, h, tileU, tileV);

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
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 10) {
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
                    emitQuad(out, axis, m0.faceDir, k, i0_abs, j0_abs, w, h, tileU, tileV);

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