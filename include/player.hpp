#pragma once
#include <glm/glm.hpp>
#include "world/world.hpp"
#include "world/chunk.hpp"
#include "world/world_config.hpp"

// --- Tunables ---
struct PlayerParams {
    float radius = 0.35f;          // world units (? 1.4 small voxels if VOXEL_SCALE=0.25)
    float height = 1.70f;
    float eyeOffset = 1.55f;          // camera height from feet
    float gravity = 18.0f;          // m/s^2 (world units/s^2)
    float maxFall = 40.0f;
    float moveSpeed = 6.0f;           // target ground speed
    float airSpeed = 3.0f;           // target air speed
    float accel = 30.0f;          // ground accel
    float airAccel = 8.0f;           // air accel
    float friction = 10.0f;          // ground friction
    float jumpSpeed = 6.5f;
};

// Simple AABB character (cylinder-ish)
struct Player {
    glm::vec3 pos{ 0.0f, 4.0f, 0.0f };   // feet position
    glm::vec3 vel{ 0.0f };
    bool onGround = false;
    bool physicsEnabled = true;

    PlayerParams p;

    // Axis-aligned half extents for AABB
    glm::vec3 halfExtents() const { return { p.radius, p.height * 0.5f, p.radius }; }

    // Step simulation by dt, with desired horizontal wishDir in camera space
    bool aabbVsWorldSlide(Player& plr, const World& w, const glm::vec3& delta);
    void simulate(const World& w, const glm::vec3& wishDir, float dt);

    // Utility to get camera/world positions
    glm::vec3 camPosition() const { return pos + glm::vec3(0, p.eyeOffset, 0); }
};

// --- collision helpers ---
bool voxelSolid(const Chunk& c, int x, int y, int z);
bool aabbVsWorldSlide(Player& plr, const Chunk& c, const glm::vec3& delta);