#pragma once
#include "world.hpp"
#include "world_config.hpp"
#include "vk_utils.hpp"

// returns: number of chunks created in that column
int ensureChunkColumn(World& w, VulkanContext& ctx, int cx, int cz);

// returns: total created within the radius
int streamEnsureAround(World& w, VulkanContext& ctx, int centerCx, int centerCz, int view);

// returns: total destroyed beyond the radius
int streamUnloadFar(World& w, int centerCx, int centerCz, int view);