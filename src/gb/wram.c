#include <gb/gb.h>
#include <string.h>
#include "wram.h"

static const uint8_t BITM[8] = {1, 2, 4, 8, 16, 32, 64, 128};

uint8_t grid_w, grid_h;

uint8_t grid_terrain(uint8_t x, uint8_t y)
{
    uint8_t o = SVBK_REG, v;
    SVBK_REG = WB_TERRAIN;
    v = W2_TERRAIN[(uint16_t)y * grid_w + x];
    SVBK_REG = o;
    return v;
}

uint8_t grid_occ(uint8_t x, uint8_t y)
{
    uint16_t i = (uint16_t)y * grid_w + x;
    uint8_t o = SVBK_REG, m = BITM[i & 7], v = 0;
    SVBK_REG = WB_SCRATCH;
    if (W7_OCC_A[i >> 3] & m) v = SIDE_A;
    else if (W7_OCC_E[i >> 3] & m) v = SIDE_E;
    SVBK_REG = o;
    return v;
}

void grid_row(uint8_t y, uint8_t x0, uint8_t n, uint8_t *ter, uint8_t *occ)
{
    uint16_t i = (uint16_t)y * grid_w + x0;
    uint8_t o = SVBK_REG, k;
    const uint8_t *src = W2_TERRAIN + i;
    SVBK_REG = WB_TERRAIN;
    for (k = 0; k < n; k++) ter[k] = src[k];
    SVBK_REG = WB_SCRATCH;
    for (k = 0; k < n; k++, i++) {
        uint8_t m = BITM[i & 7];
        occ[k] = (W7_OCC_A[i >> 3] & m) ? SIDE_A : ((W7_OCC_E[i >> 3] & m) ? SIDE_E : 0);
    }
    SVBK_REG = o;
}

void occ_set(uint8_t x, uint8_t y, uint8_t side)
{
    uint16_t i = (uint16_t)y * grid_w + x;
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    if (side == SIDE_A) W7_OCC_A[i >> 3] |= (uint8_t)(1u << (i & 7));
    else W7_OCC_E[i >> 3] |= (uint8_t)(1u << (i & 7));
    SVBK_REG = o;
}

void occ_clear(uint8_t x, uint8_t y)
{
    uint16_t i = (uint16_t)y * grid_w + x;
    uint8_t o = SVBK_REG, m = (uint8_t)~(1u << (i & 7));
    SVBK_REG = WB_SCRATCH;
    W7_OCC_A[i >> 3] &= m;
    W7_OCC_E[i >> 3] &= m;
    SVBK_REG = o;
}

uint16_t fld_get(uint16_t i)
{
    uint8_t o = SVBK_REG;
    uint16_t v;
    SVBK_REG = (uint8_t)(WB_FIELD + (i >> 11));
    v = ((uint16_t *)0xD000)[i & 0x7FF];
    SVBK_REG = o;
    return v;
}

void fld_set(uint16_t i, uint16_t v)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = (uint8_t)(WB_FIELD + (i >> 11));
    ((uint16_t *)0xD000)[i & 0x7FF] = v;
    SVBK_REG = o;
}

uint16_t fq_get(uint16_t i)
{
    uint8_t o = SVBK_REG;
    uint16_t v;
    SVBK_REG = (uint8_t)(WB_QUEUE + (i >> 11));
    v = ((uint16_t *)0xD000)[i & 0x7FF];
    SVBK_REG = o;
    return v;
}

void fq_set(uint16_t i, uint16_t v)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = (uint8_t)(WB_QUEUE + (i >> 11));
    ((uint16_t *)0xD000)[i & 0x7FF] = v;
    SVBK_REG = o;
}

uint8_t fin_test(uint16_t i)
{
    uint8_t o = SVBK_REG, v;
    SVBK_REG = WB_SCRATCH;
    v = (uint8_t)((W7_INQ[i >> 3] >> (i & 7)) & 1u);
    SVBK_REG = o;
    return v;
}

void fin_set(uint16_t i)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    W7_INQ[i >> 3] |= (uint8_t)(1u << (i & 7));
    SVBK_REG = o;
}

void fin_clear(uint16_t i)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    W7_INQ[i >> 3] &= (uint8_t)~(1u << (i & 7));
    SVBK_REG = o;
}

void wram_init_grid(uint8_t w, uint8_t h, const uint8_t *terrain)
{
    uint8_t o = SVBK_REG;
    grid_w = w; grid_h = h;
    SVBK_REG = WB_TERRAIN;
    memset(W2_TERRAIN, 0, 4096);
    memcpy(W2_TERRAIN, terrain, (uint16_t)w * h);
    SVBK_REG = WB_SCRATCH;
    memset((void *)0xD000, 0, 0x600);   /* occupancy bitmaps + inqueue bits */
    SVBK_REG = o;
}
