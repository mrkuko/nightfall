/* noise.h — hash noise and value noise (no tables, deterministic from the seed) */
#ifndef NF_NOISE_H
#define NF_NOISE_H

#include <stdint.h>
#include <math.h>

static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t hash2i(int x, int y, uint32_t seed)
{
    return hash32((uint32_t)x * 0x8da6b343U ^ hash32((uint32_t)y * 0xd8163841U ^ seed));
}

/* [0, 1) */
static inline float hash2f(int x, int y, uint32_t seed)
{
    return (float)(hash2i(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}

static inline float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

/* Smooth value noise, output [0, 1). */
static inline float vnoise2(float x, float y, uint32_t seed)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float tx = smooth01(x - (float)xi), ty = smooth01(y - (float)yi);
    float a = hash2f(xi, yi, seed),     b = hash2f(xi + 1, yi, seed);
    float c = hash2f(xi, yi + 1, seed), d = hash2f(xi + 1, yi + 1, seed);
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

/* Fractal noise (octaves), output roughly [0, 1). */
static inline float fbm2(float x, float y, int oct, uint32_t seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int i = 0; i < oct; i++) {
        sum += vnoise2(x, y, seed + (uint32_t)i * 101u) * amp;
        norm += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return sum / norm;
}

/* 1D smooth noise over time (wind, irregularity). Output [-1, 1). */
static inline float vnoise1s(float t, uint32_t seed)
{
    return vnoise2(t, 0.5f, seed) * 2.0f - 1.0f;
}

#endif
