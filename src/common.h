/* common.h — basic types shared across the whole project */
#ifndef NF_COMMON_H
#define NF_COMMON_H

#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t r, g, b; } rgb_t;

/* Sprite pixel: colour, transparency, depth offset, flags. */
typedef struct {
    uint8_t r, g, b, a;   /* a: 0 = empty, 255 = opaque, in between = translucent (no z write) */
    int8_t  z;            /* depth offset from the object's base z (+ = closer to the camera) */
    uint8_t f;            /* PX_* flags */
} px_t;

#define PX_GLOW 0x01      /* emits light: not affected by fog */

#define Z_EMPTY INT16_MIN

static inline rgb_t mkrgb(int r, int g, int b)
{
    rgb_t c;
    c.r = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
    c.g = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
    c.b = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
    return c;
}

static inline rgb_t rgb_hex(uint32_t h)
{
    return mkrgb((int)((h >> 16) & 255), (int)((h >> 8) & 255), (int)(h & 255));
}

static inline rgb_t rgb_mix(rgb_t a, rgb_t b, float t)
{
    return mkrgb((int)(a.r + (b.r - a.r) * t + 0.5f),
                 (int)(a.g + (b.g - a.g) * t + 0.5f),
                 (int)(a.b + (b.b - a.b) * t + 0.5f));
}

static inline rgb_t rgb_add(rgb_t a, int d)
{
    return mkrgb(a.r + d, a.g + d, a.b + d);
}

static inline rgb_t rgb_scale(rgb_t a, float s)
{
    return mkrgb((int)(a.r * s), (int)(a.g * s), (int)(a.b * s));
}

static inline int rgb_eq(rgb_t a, rgb_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

#define NF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NF_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NF_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#endif
