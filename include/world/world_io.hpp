#pragma once
#include <string>
#include "world.hpp"

// Simple binary format (little-endian)
// [Header]
//   char magic[4] = "VWLD"
//   uint32_t version = 1
//   int32_t chunkSize, chunkHeight
//   uint32_t chunkCount
// [Per chunk]
//   int32_t cx, cy, cz
//   uint32_t rleCount
//   (rleCount times) { uint16_t id; uint32_t runLen; }

bool worldSaveToFile(const World& w, const std::string& path, std::string* err = nullptr);
bool worldLoadFromFile(World& w, const std::string& path, std::string* err = nullptr);