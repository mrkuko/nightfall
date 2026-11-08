/* rng.h — deterministic pseudo-random generator (splitmix64) */
#ifndef NF_RNG_H
#define NF_RNG_H

#include <stdint.h>

typedef struct { uint64_t s; } rng_t;

static inline rng_t rng_seed(uint64_t seed)
{
    rng_t r;
    r.s = seed ^ 0x9E3779B97F4A7C15ull;
    return r;
}

static inline uint64_t rng_next(rng_t *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static inline uint32_t rng_u32(rng_t *r) { return (uint32_t)(rng_next(r) >> 32); }

/* [0, 1) */
static inline float rng_float(rng_t *r) { return (float)(rng_next(r) >> 40) * (1.0f / 16777216.0f); }

/* [a, b) */
static inline float rng_range(rng_t *r, float a, float b) { return a + (b - a) * rng_float(r); }

/* [0, n) */
static inline int rng_int(rng_t *r, int n) { return n <= 0 ? 0 : (int)(rng_u32(r) % (uint32_t)n); }

/* [a, b] inclusive */
static inline int rng_between(rng_t *r, int a, int b) { return a + rng_int(r, b - a + 1); }

static inline int rng_chance(rng_t *r, float p) { return rng_float(r) < p; }

/* Derived seed so that subsystems do not share a sequence. */
static inline uint64_t rng_derive(uint64_t seed, uint64_t salt)
{
    rng_t t = rng_seed(seed ^ (salt * 0xD1B54A32D192ED03ull));
    return rng_next(&t);
}

#endif
