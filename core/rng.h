#ifndef FP_RNG_H
#define FP_RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s;
} fp_rng;

static inline void fp_rng_seed(fp_rng *r, uint64_t seed)
{
    r->s = seed;
}

static inline uint64_t fp_rng_u64(fp_rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* [0,1) from the top 24 bits so every value is exactly representable */
static inline float fp_rng_f32(fp_rng *r)
{
    return (float)(fp_rng_u64(r) >> 40) * (1.0f / 16777216.0f);
}

#endif
