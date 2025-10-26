#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

static void writeDestroyWavIfMissing(const std::string& path) {
    namespace fs = std::filesystem;
    try {
        fs::create_directories(fs::path(path).parent_path());
    }
    catch (...) {}

    if (fs::exists(path)) return; // already there

    const uint32_t sampleRate = 44100;
    const float durationSec = 0.18f;
    const uint32_t n = (uint32_t)(sampleRate * durationSec);

    // Synthesize: noise burst + downward sine "thud" + tiny click, with decay envelope
    std::vector<int16_t> pcm(n);
    uint32_t rng = 0x1234567u; // simple LCG for deterministic noise
    auto rnd = [&]() {
        rng = rng * 1664525u + 1013904223u;
        // map to [-1,1]
        return ((int32_t)(rng >> 1) - (int32_t)(1u << 30)) / float(1u << 30);
        };

    float prevY = 0.0f, prevX = 0.0f;
    const float hpAlpha = 0.995f;

    float peak = 1e-9f;
    std::vector<float> buf(n);

    for (uint32_t i = 0; i < n; ++i) {
        float t = i / float(sampleRate);

        // envelope
        float envNoise = std::exp(-t * 20.0f); // fast decay
        float envThud = std::exp(-t * 12.0f);

        // noise
        float noise = rnd() * envNoise * 0.4f;

        // downward sine sweep
        float f0 = 320.0f, f1 = 100.0f;
        float f = f0 + (f1 - f0) * (i / float(n));
        static float phase = 0.0f;
        phase += 2.0f * 3.1415926535f * f / sampleRate;
        float thud = 0.6f * std::sin(phase) * envThud;

        // short click at start
        float click = 0.0f;
        if (i < (uint32_t)(0.004f * sampleRate)) {
            float w = (float)i / (0.004f * sampleRate);
            // tiny Hann
            click = 0.6f * (0.5f - 0.5f * std::cos(2.0f * 3.1415926535f * w));
        }

        float y = noise + thud + click;

        // 1st-order highpass to remove DC
        float hp = hpAlpha * (prevY + y - prevX);
        prevY = hp; prevX = y;

        buf[i] = hp;
        peak = std::max(peak, std::fabs(hp));
    }

    // Normalize to -0.9 FS and convert to int16
    for (uint32_t i = 0; i < n; ++i) {
        float s = (buf[i] / peak) * 0.9f;
        if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
        pcm[i] = (int16_t)std::lround(s * 32767.0f);
    }

    // Write minimal WAV (PCM 16-bit mono)
    struct WAVHeader {
        char     riff[4] = { 'R','I','F','F' };
        uint32_t chunkSize;
        char     wave[4] = { 'W','A','V','E' };
        char     fmt_[4] = { 'f','m','t',' ' };
        uint32_t subchunk1Size = 16;
        uint16_t audioFormat = 1;  // PCM
        uint16_t numChannels = 1;
        uint32_t sampleRate = 44100;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample = 16;
        char     data[4] = { 'd','a','t','a' };
        uint32_t dataSize;
    } hdr;

    hdr.sampleRate = sampleRate;
    hdr.blockAlign = hdr.numChannels * (hdr.bitsPerSample / 8);
    hdr.byteRate = hdr.sampleRate * hdr.blockAlign;
    hdr.dataSize = (uint32_t)(pcm.size() * hdr.blockAlign);
    hdr.chunkSize = 36 + hdr.dataSize;

    std::ofstream f(path, std::ios::binary);
    f.write((const char*)&hdr, sizeof(hdr));
    f.write((const char*)pcm.data(), hdr.dataSize);
    f.close();
}