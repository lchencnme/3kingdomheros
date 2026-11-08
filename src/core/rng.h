/* xorshift32 RNG (design doc 4.4). Every draw is counted for replay logs. */
#ifndef CORE_RNG_H
#define CORE_RNG_H
#include <stdint.h>

#define RNG_ZERO_SEED 0x6D2B79F5ul

typedef struct {
    uint32_t state;
    uint32_t draws;
} Rng;

void rng_seed(Rng *r, uint32_t seed);
uint32_t rng_next(Rng *r);
/* rand(n) = next() % n; n must be > 0 */
uint16_t rng_rand(Rng *r, uint16_t n);
#endif
