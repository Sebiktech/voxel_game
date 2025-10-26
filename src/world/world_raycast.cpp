#include "world/world_raycast.hpp"
#include <cmath>
#include <algorithm>

static inline void worldToVoxel(const glm::vec3& w, int& x, int& y, int& z) {
    x = (int)std::floor(w.x / VOXEL_SCALE + 0.5f);
    y = (int)std::floor(w.y / VOXEL_SCALE + 0.5f);
    z = (int)std::floor(w.z / VOXEL_SCALE + 0.5f);
}

RayHit raycastWorld(const World& w, const glm::vec3& pos, const glm::vec3& dirN, float maxDist)
{
    RayHit rh{};
    if (maxDist <= 0.0f) return rh;

    // start cell
    int x, y, z;
    worldToVoxel(pos, x, y, z);

    // step per axis
    int stepX = (dirN.x > 0) ? 1 : (dirN.x < 0 ? -1 : 0);
    int stepY = (dirN.y > 0) ? 1 : (dirN.y < 0 ? -1 : 0);
    int stepZ = (dirN.z > 0) ? 1 : (dirN.z < 0 ? -1 : 0);

    // tMax: distance to first grid boundary; tDelta: distance between crossings
    auto safeInv = [](float v) { return (std::abs(v) < 1e-8f) ? 1e30f : (1.0f / v); };
    float invX = safeInv(dirN.x), invY = safeInv(dirN.y), invZ = safeInv(dirN.z);

    auto boundary = [&](int v, int step, float posAxis) {
        // world coord of next boundary plane in that step direction, then convert to t
        float nextGrid = (v + (step > 0 ? 0.5f : -0.5f)) * VOXEL_SCALE;
        return (nextGrid - posAxis);
        };

    float tMaxX = (stepX != 0) ? boundary(x, stepX, pos.x) * invX : 1e30f;
    float tMaxY = (stepY != 0) ? boundary(y, stepY, pos.y) * invY : 1e30f;
    float tMaxZ = (stepZ != 0) ? boundary(z, stepZ, pos.z) * invZ : 1e30f;

    float tDeltaX = (stepX != 0) ? (VOXEL_SCALE * std::abs(invX)) : 1e30f;
    float tDeltaY = (stepY != 0) ? (VOXEL_SCALE * std::abs(invY)) : 1e30f;
    float tDeltaZ = (stepZ != 0) ? (VOXEL_SCALE * std::abs(invZ)) : 1e30f;

    // DDA
    int lastNx = 0, lastNy = 0, lastNz = 0;
    int lastX = x, lastY = y, lastZ = z;
    float t = 0.0f;

    const int maxSteps = 2048;
    for (int i = 0; i < maxSteps && t <= maxDist; ++i) {
        // solid at current cell?
        if (worldVoxelSolid(w, x, y, z)) {
            rh.hit = true;
            rh.t = t;
            rh.vx = x; rh.vy = y; rh.vz = z;
            rh.nx = lastNx; rh.ny = lastNy; rh.nz = lastNz; // normal points toward us
            rh.ex = lastX; rh.ey = lastY; rh.ez = lastZ;    // empty cell we came from
            return rh;
        }

        // advance to next boundary
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {           // X
                lastX = x; lastY = y; lastZ = z;
                x += stepX; t = tMaxX; tMaxX += tDeltaX;
                lastNx = -stepX; lastNy = 0; lastNz = 0;
            }
            else {                        // Z
                lastX = x; lastY = y; lastZ = z;
                z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ;
                lastNx = 0; lastNy = 0; lastNz = -stepZ;
            }
        }
        else {
            if (tMaxY < tMaxZ) {            // Y
                lastX = x; lastY = y; lastZ = z;
                y += stepY; t = tMaxY; tMaxY += tDeltaY;
                lastNx = 0; lastNy = -stepY; lastNz = 0;
            }
            else {                         // Z
                lastX = x; lastY = y; lastZ = z;
                z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ;
                lastNx = 0; lastNy = 0; lastNz = -stepZ;
            }
        }
    }
    return rh; // no hit
}