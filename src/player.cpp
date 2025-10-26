#include "player.hpp"
#include <algorithm>
#include <cmath>

// world?voxel index
static inline void worldToVoxel(const glm::vec3& w, int& x, int& y, int& z) {
    x = (int)std::floor(w.x / VOXEL_SCALE + 0.5f);
    y = (int)std::floor(w.y / VOXEL_SCALE + 0.5f);
    z = (int)std::floor(w.z / VOXEL_SCALE + 0.5f);
}

bool voxelSolid(const World& w, int x, int y, int z) {
    return worldVoxelSolid(w, x, y, z); 
}

// sweep AABB along one axis and stop before hitting a solid voxel
static void sweepAxis(Player& plr, const World& w, int axis, float& move) {
    if (move == 0.0f) return;

    glm::vec3 he = plr.halfExtents();
    glm::vec3 startMin = plr.pos - glm::vec3(he.x, 0.0f, he.z); // feet xz
    glm::vec3 boxMin = { plr.pos.x - he.x, plr.pos.y, plr.pos.z - he.z };
    glm::vec3 boxMax = { plr.pos.x + he.x, plr.pos.y + 2.0f * he.y, plr.pos.z + he.z };

    // expand in move dir
    if (axis == 0) { boxMin.x += std::min(0.0f, move); boxMax.x += std::max(0.0f, move); }
    if (axis == 1) { boxMin.y += std::min(0.0f, move); boxMax.y += std::max(0.0f, move); }
    if (axis == 2) { boxMin.z += std::min(0.0f, move); boxMax.z += std::max(0.0f, move); }

    // voxel bounds to test
    int x0, y0, z0, x1, y1, z1;
    worldToVoxel(boxMin, x0, y0, z0);
    worldToVoxel(boxMax, x1, y1, z1);
    x0--; y0--; z0--; x1++; y1++; z1++; // small pad

    // step along axis in small increments per-voxel
    const float step = (move > 0 ? VOXEL_SCALE * 0.5f : -VOXEL_SCALE * 0.5f);
    float advanced = 0.0f;
    int iters = 0, maxIters = 128;

    while ((move > 0 ? advanced < move : advanced > move) && iters++ < maxIters) {
        float stepTry = move - advanced;
        if (std::abs(stepTry) > std::abs(step)) stepTry = step;

        // propose new position
        glm::vec3 newPos = plr.pos;
        if (axis == 0) newPos.x += stepTry;
        if (axis == 1) newPos.y += stepTry;
        if (axis == 2) newPos.z += stepTry;

        // AABB at newPos
        glm::vec3 he = plr.halfExtents();
        glm::vec3 mn = { newPos.x - he.x, newPos.y, newPos.z - he.z };
        glm::vec3 mx = { newPos.x + he.x, newPos.y + 2.0f * he.y, newPos.z + he.z };

        // check any overlap with solid voxels inside test window
        bool hit = false;
        int cx0, cy0, cz0, cx1, cy1, cz1;
        worldToVoxel(mn, cx0, cy0, cz0);
        worldToVoxel(mx, cx1, cy1, cz1);

        for (int z = cz0; z <= cz1 && !hit; ++z)
            for (int y = cy0; y <= cy1 && !hit; ++y)
                for (int x = cx0; x <= cx1 && !hit; ++x) {
                    if (!voxelSolid(w, x, y, z)) continue;
                    // voxel AABB
                    glm::vec3 vmn = glm::vec3((x - 0.5f) * VOXEL_SCALE, (y - 0.5f) * VOXEL_SCALE, (z - 0.5f) * VOXEL_SCALE);
                    glm::vec3 vmx = vmn + glm::vec3(VOXEL_SCALE);
                    // overlap test
                    if (mn.x < vmx.x && mx.x > vmn.x &&
                        mn.y < vmx.y && mx.y > vmn.y &&
                        mn.z < vmx.z && mx.z > vmn.z) {
                        hit = true;
                    }
                }

        if (hit) {
            // stop before collision
            break;
        }
        else {
            plr.pos = newPos;
            advanced += stepTry;
        }
    }

    // zero out velocity on blocked axis
    if ((move > 0 && advanced < move) || (move < 0 && advanced > move)) {
        if (axis == 0) plr.vel.x = 0.0f;
        if (axis == 1) plr.vel.y = 0.0f;
        if (axis == 2) plr.vel.z = 0.0f;
    }
}

bool Player::aabbVsWorldSlide(Player& plr, const World& w, const glm::vec3& delta) {
    glm::vec3 before = plr.pos;
    float dx = delta.x, dy = delta.y, dz = delta.z;
    sweepAxis(plr, w, 1, dy);
    sweepAxis(plr, w, 0, dx);
    sweepAxis(plr, w, 2, dz);
    return (plr.pos.x != before.x) || (plr.pos.y != before.y) || (plr.pos.z != before.z);
}

void Player::simulate(const World& w, const glm::vec3& wishDir, float dt) {
    // wishDir is a unit vector in XZ plane (camera-relative), y ignored
    glm::vec2 v2 = { vel.x, vel.z };
    glm::vec2 wish = { wishDir.x, wishDir.z };
    float target = onGround ? p.moveSpeed : p.airSpeed;
    float accel = onGround ? p.accel : p.airAccel;

    // accelerate towards target
    if (glm::length(wish) > 0.0f) {
        glm::vec2 w = glm::normalize(wish) * target;
        glm::vec2 dv = w - v2;
        float maxStep = accel * dt;
        if (glm::length(dv) > maxStep) dv = glm::normalize(dv) * maxStep;
        v2 += dv;
    }
    else if (onGround) {
        // friction
        float spd = glm::length(v2);
        float drop = p.friction * dt * spd;
        float newSpd = std::max(0.0f, spd - drop);
        if (spd > 0.0f) v2 *= (newSpd / spd);
    }
    vel.x = v2.x; vel.z = v2.y;

    // gravity
    vel.y -= p.gravity * dt;
    if (vel.y < -p.maxFall) vel.y = -p.maxFall;

    // integrate with collision
    glm::vec3 delta = vel * dt;
    glm::vec3 oldPos = pos;
    aabbVsWorldSlide(*this, w, delta);

    // ground check: cast tiny step down
    {
        glm::vec3 probe = pos;
        float eps = 0.02f;
        probe.y -= eps;
        glm::vec3 he = halfExtents();
        // check feet box slightly below
        glm::vec3 mn = { probe.x - he.x, probe.y, probe.z - he.z };
        glm::vec3 mx = { probe.x + he.x, probe.y + eps * 2.0f, probe.z + he.z };
        int x0, y0, z0, x1, y1, z1; worldToVoxel(mn, x0, y0, z0); worldToVoxel(mx, x1, y1, z1);
        bool footHit = false;
        for (int z = z0; z <= z1 && !footHit; ++z)
            for (int y = y0; y <= y1 && !footHit; ++y)
                for (int x = x0; x <= x1 && !footHit; ++x) {
                    if (voxelSolid(w, x, y, z)) footHit = true;
                }
        onGround = footHit && std::abs(vel.y) < 1.0f;
        if (onGround && vel.y < 0.0f) vel.y = 0.0f;
    }
}