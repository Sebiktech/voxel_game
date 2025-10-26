#include "world/world_io.hpp"
#include "world/world_config.hpp"
#include "world/mesher.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>

#pragma pack(push, 1)
struct WorldHeader {
    char magic[4];
    uint32_t version;
    int32_t chunkSize;
    int32_t chunkHeight;
    uint32_t chunkCount;
};
#pragma pack(pop)

static void writeU32(std::ofstream& f, uint32_t v) { f.write((const char*)&v, sizeof(v)); }
static void writeI32(std::ofstream& f, int32_t v) { f.write((const char*)&v, sizeof(v)); }
static void writeU16(std::ofstream& f, uint16_t v) { f.write((const char*)&v, sizeof(v)); }

static bool readU32(std::ifstream& f, uint32_t& v) { return bool(f.read((char*)&v, sizeof(v))); }
static bool readI32(std::ifstream& f, int32_t& v) { return bool(f.read((char*)&v, sizeof(v))); }
static bool readU16(std::ifstream& f, uint16_t& v) { return bool(f.read((char*)&v, sizeof(v))); }

static bool ensureParentDir(const std::string& path, std::string* err) {
    std::error_code ec;
    std::filesystem::path p(path);
    auto parent = p.parent_path();
    if (parent.empty()) return true; // file in cwd, ok
    if (std::filesystem::exists(parent)) return true;
    if (!std::filesystem::create_directories(parent, ec)) {
        if (err) *err = "Failed to create directory '" + parent.string() + "': " + ec.message();
        return false;
    }
    return true;
}

bool worldSaveToFile(const World& w, const std::string& path, std::string* err) {
    if (!ensureParentDir(path, err)) return false;

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = std::string("Failed to open file for write: '") + path + "' (" + std::strerror(errno) + ")";
        return false;
    }

    WorldHeader hdr{};
    std::memcpy(hdr.magic, "VWLD", 4);
    hdr.version = 1;
    hdr.chunkSize = CHUNK_SIZE;
    hdr.chunkHeight = CHUNK_HEIGHT;
    hdr.chunkCount = (uint32_t)w.map.size(); // adjust if your container name differs
    f.write((const char*)&hdr, sizeof(hdr));

    // For each chunk: RLE of voxels (x,z,y order or whatever your mesher expects)
    for (auto const& kv : w.map) {
        const WorldKey& key = kv.first;
        const WorldChunk& wc = *kv.second;

        writeI32(f, key.cx); writeI32(f, key.cy); writeI32(f, key.cz);

        // Flatten voxels to linear and RLE
        std::vector<uint16_t> tmp;
        tmp.reserve((size_t)CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT);

        for (int y = 0; y < CHUNK_HEIGHT; ++y)
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    tmp.push_back((uint16_t)wc.data.get(x, y, z));
                }

        // RLE
        struct Run { uint16_t id; uint32_t len; };
        std::vector<Run> runs;
        runs.reserve(tmp.size() / 4 + 1);
        uint16_t cur = tmp.empty() ? 0 : tmp[0];
        uint32_t len = 0;
        for (uint16_t v : tmp) {
            if (v == cur && len < 0xFFFFFFFFu) { ++len; }
            else { runs.push_back({ cur, len }); cur = v; len = 1; }
        }
        if (len) runs.push_back({ cur, len });

        writeU32(f, (uint32_t)runs.size());
        for (auto& r : runs) { writeU16(f, r.id); writeU32(f, r.len); }
    }

    return true;
}

bool worldLoadFromFile(World& w, const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "Failed to open file for read"; return false; }

    WorldHeader hdr{};
    if (!f.read((char*)&hdr, sizeof(hdr))) { if (err) *err = "Corrupt header"; return false; }
    if (std::memcmp(hdr.magic, "VWLD", 4) != 0 || hdr.version != 1) {
        if (err) *err = "Unsupported world format"; return false;
    }
    if (hdr.chunkSize != CHUNK_SIZE || hdr.chunkHeight != CHUNK_HEIGHT) {
        if (err) *err = "Mismatched chunk dimensions"; return false;
    }

    // Clear current world (defer-destroy GPU buffers if you use that system)
    w.clearAllChunks();   // implement this: destroy CPU data + enqueue GPU buffers for destroy

    for (uint32_t i = 0; i < hdr.chunkCount; ++i) {
        int32_t cx, cy, cz;
        if (!readI32(f, cx) || !readI32(f, cy) || !readI32(f, cz)) { if (err) *err = "Corrupt chunk key"; return false; }
        uint32_t rleCount;
        if (!readU32(f, rleCount)) { if (err) *err = "Corrupt RLE header"; return false; }

        // Reconstruct flat array
        const size_t total = (size_t)CHUNK_SIZE * CHUNK_SIZE * CHUNK_HEIGHT;
        std::vector<uint16_t> flat; flat.reserve(total);
        for (uint32_t r = 0; r < rleCount; ++r) {
            uint16_t id; uint32_t len;
            if (!readU16(f, id) || !readU32(f, len)) { if (err) *err = "Corrupt RLE run"; return false; }
            if (flat.size() + len > total) { if (err) *err = "RLE overflow"; return false; }
            flat.insert(flat.end(), len, id);
        }
        if (flat.size() != total) { if (err) *err = "Size mismatch after RLE"; return false; }

        // Allocate / insert chunk into world
        WorldKey key{ cx, cy, cz };
        WorldChunk* wc = w.createChunk(key);  // implement: make (or get) a chunk with this key
        if (!wc) { if (err) *err = "Failed to create chunk"; return false; }

        // Fill voxel data
        size_t idx = 0;
        for (int y = 0; y < CHUNK_HEIGHT; ++y)
            for (int z = 0; z < CHUNK_SIZE; ++z)
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                    wc->data.set(x, y, z, (BlockID)flat[idx++]);
                }

        // Rebuild CPU mesh (with offset) and mark for upload
        wc->meshCPU = meshChunkAt(wc->data, cx, cy, cz);
        wc->needsUpload = true;
    }

    return true;
}