#pragma once
#include <glm/glm.hpp>
#include "world.hpp"
#include "world_config.hpp"

struct RayHit {
    bool  hit = false;
    float t = 0.0f;       // distance in world units
    int   vx = 0, vy = 0, vz = 0; // hit voxel coords (smallest grid)
    int   nx = 0, ny = 0, nz = 0; // face normal (points out of the solid)
    int   ex = 0, ey = 0, ez = 0; // adjacent empty voxel (for placement)
};

// DDA through the voxel grid; dir must be normalized
RayHit raycastWorld(const World& w, const glm::vec3& pos, const glm::vec3& dir, float maxDist);