#pragma once
#include "world.hpp"
#include "world_config.hpp"
#include "vk_utils.hpp"

// Ensure a column (cx,cz) for all needed cy (if you have vertical stacking)
bool ensureChunkColumn(World& w, VulkanContext& ctx, int cx, int cz);

// Load/unload around center
void streamEnsureAround(World& w, VulkanContext& ctx, int centerCx, int centerCz, int view);
void streamUnloadFar(World& w, int centerCx, int centerCz, int view);