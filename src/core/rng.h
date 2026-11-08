/* xorshift32 RNG (design doc 4.4). Every draw is counted for replay logs. */
#ifndef CORE_RNG_H
#define CORE_RNG_H
#include <stdint.h>
#include "platform.h"

#define RNG_ZERO_SEED 0x6D2B79F5ul

typedef struct {
    uint32_t state;
    uint32_t draws;
} Rng;

void rng_seed(Rng *r, uint32_t seed) CORE_API;
uint32_t rng_next(Rng *r) CORE_API;
/* rand(n) = next() % n; n must be > 0 */
uint16_t rng_rand(Rng *r, uint16_t n) CORE_API;
#endif
