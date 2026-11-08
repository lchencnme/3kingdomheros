#include "rng.h"

void rng_seed(Rng *r, uint32_t seed)
{
    r->state = seed ? seed : RNG_ZERO_SEED;
    r->draws = 0;
}

uint32_t rng_next(Rng *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    r->draws++;
    return x;
}

uint16_t rng_rand(Rng *r, uint16_t n)
{
    return (uint16_t)(rng_next(r) % (uint32_t)n);
}
