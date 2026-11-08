/* Host implementation of the grid/field accessors using plain arrays. */
#include "../../src/core/grid.h"

uint8_t grid_w, grid_h;
uint8_t host_terrain[4096];
uint8_t host_occ[4096];
static uint16_t host_field[4096];
static uint16_t host_queue[4096];
static uint8_t host_inq[512];

uint8_t grid_terrain(uint8_t x, uint8_t y) PLATFORM_API { return host_terrain[(uint16_t)y * grid_w + x]; }
uint8_t grid_occ(uint8_t x, uint8_t y) PLATFORM_API { return host_occ[(uint16_t)y * grid_w + x]; }
uint16_t fld_get(uint16_t i) PLATFORM_API { return host_field[i]; }
void fld_set(uint16_t i, uint16_t v) PLATFORM_API { host_field[i] = v; }
uint16_t fq_get(uint16_t i) PLATFORM_API { return host_queue[i]; }
void fq_set(uint16_t i, uint16_t v) PLATFORM_API { host_queue[i] = v; }
uint8_t fin_test(uint16_t i) PLATFORM_API { return (host_inq[i >> 3] >> (i & 7)) & 1; }
void fin_set(uint16_t i) PLATFORM_API { host_inq[i >> 3] |= (uint8_t)(1u << (i & 7)); }
void fin_clear(uint16_t i) PLATFORM_API { host_inq[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
void grid_row(uint8_t y, uint8_t x0, uint8_t n, uint8_t *ter, uint8_t *occ) PLATFORM_API
{
    uint8_t i;
    for (i = 0; i < n; i++) { ter[i] = host_terrain[(uint16_t)y * grid_w + x0 + i]; occ[i] = host_occ[(uint16_t)y * grid_w + x0 + i]; }
}
