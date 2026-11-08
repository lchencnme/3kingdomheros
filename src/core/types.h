/* Portable integer helpers (design doc 1.5): 32-bit intermediates, mathematical floor division. */
#ifndef CORE_TYPES_H
#define CORE_TYPES_H
#include <stdint.h>

/* floor(a/b) for b>0; C '/' truncates toward zero, which is wrong for negative a. */
static inline int32_t fdiv32(int32_t a, int32_t b)
{
    int32_t q = a / b;
    if ((a % b) != 0 && (a < 0)) q--;
    return q;
}

static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
#endif
