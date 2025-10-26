#pragma once
#include <glm/glm.hpp>
#include <cstdint>

inline uint32_t hash32(uint32_t x) {
    x += 0x9e3779b9u; x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
inline float rand01(uint32_t x) { return (hash32(x) & 0x00FFFFFFu) / float(0x01000000u); }

inline float valueNoise2D(int x, int y, uint32_t seed) {
    uint32_t h = hash32(seed ^ hash32(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u));
    return (h & 0x00FFFFFFu) / float(0x01000000u);
}
inline float smoothValue2D(float x, float y, uint32_t seed) {
    int ix = int(floor(x)), iy = int(floor(y));
    float fx = x - ix, fy = y - iy;
    auto n = [&](int dx, int dy) { return valueNoise2D(ix + dx, iy + dy, seed); };
    auto lerp = [&](float a, float b, float t) { return a + (b - a) * t; };
    auto fade = [&](float t) { return t * t * (3.f - 2.f * t); };
    float nx0 = lerp(n(0, 0), n(1, 0), fade(fx));
    float nx1 = lerp(n(0, 1), n(1, 1), fade(fx));
    return lerp(nx0, nx1, fade(fy));
}
inline float fbm2(float x, float y, uint32_t seed, int oct = 5, float lac = 2.0f, float gain = 0.5f) {
    float a = 1.f, f = 1.f, s = 0.f, n = 0.f;
    for (int i = 0; i < oct; i++) { n += a * smoothValue2D(x * f, y * f, seed + i * 1013u); s += a; a *= gain; f *= lac; }
    return n / s;
}