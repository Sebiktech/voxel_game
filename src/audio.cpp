#include "audio.hpp"
#include <unordered_map>
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"

static ma_engine gEngine;
static std::unordered_map<std::string, std::string> gEventPaths;

bool Audio::init() {
    ma_result r = ma_engine_init(nullptr, &gEngine);
    ready = (r == MA_SUCCESS);
    if (!ready) std::cerr << "[Audio] ma_engine_init failed: " << r << "\n";
    return ready;
}

void Audio::shutdown() {
    if (ready) {
        ma_engine_uninit(&gEngine);
        ready = false;
    }
    gEventPaths.clear();
}

bool Audio::loadEvent(const std::string& name, const std::string& path) {
    gEventPaths[name] = path;   // simple: stream from file on play
    return true;
}

void Audio::play(const std::string& name, float volume, float /*pitch*/) {
    if (!ready) return;
    auto it = gEventPaths.find(name);
    if (it == gEventPaths.end()) return;
    // miniaudio streams the file when you call this; simple and fine for sfx.
    ma_engine_play_sound(&gEngine, it->second.c_str(), nullptr);
    // volume control (optional): use per-sound object if you need it persistent.
}