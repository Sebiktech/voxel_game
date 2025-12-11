#pragma once
#include <glm/glm.hpp>

// Forward declarations
struct World;
struct VulkanContext;

// Main streaming function - call this every frame!
void worldStreamTick(World& w, VulkanContext& ctx,
    const glm::vec3& camPos, const glm::vec3& camFwd);

// Helper functions (you can keep using these directly if needed)
// returns: number of chunks created in that column
int ensureChunkColumn(World& w, VulkanContext& ctx, int cx, int cz);

// returns: total created within the radius
int streamEnsureAround(World& w, VulkanContext& ctx, int centerCx, int centerCz, int view);

// returns: total destroyed beyond the radius
int streamUnloadFar(World& w, int centerCx, int centerCz, int view);