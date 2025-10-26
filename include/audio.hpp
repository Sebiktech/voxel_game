#pragma once
#include <string>

struct Audio {
    bool init();                 // returns false if device init fails
    void shutdown();
    bool loadEvent(const std::string& name, const std::string& path);  // just remembers the path
    void play(const std::string& name, float volume = 1.0f, float pitch = 1.0f);

private:
    bool ready = false;
};