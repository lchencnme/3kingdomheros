/* Host implementation of the grid/field accessors using plain arrays. */
#include "../../src/core/path.h"

uint8_t grid_w, grid_h;
uint8_t host_terrain[4096];
uint8_t host_occ[4096];

uint8_t grid_terrain(uint8_t x, uint8_t y) PLATFORM_API { return host_terrain[(uint16_t)y * grid_w + x]; }
uint8_t grid_occ(uint8_t x, uint8_t y) PLATFORM_API { return host_occ[(uint16_t)y * grid_w + x]; }
static uint8_t host_fc[8192], host_fq[8192];
uint8_t *fc_ptr(uint16_t i) { return &host_fc[(uint32_t)i * 2u]; }
uint8_t *fq_ptr(uint16_t k) { return &host_fq[(uint32_t)k * 2u]; }
uint16_t fld_get(uint16_t i) PLATFORM_API { uint8_t d = host_fc[(uint32_t)i * 2u]; return d == FC_UNREACHED ? FIELD_INF : d; }
void grid_row(uint8_t y, uint8_t x0, uint8_t n, uint8_t *ter, uint8_t *occ) PLATFORM_API
{
    uint8_t i;
    for (i = 0; i < n; i++) { ter[i] = host_terrain[(uint16_t)y * grid_w + x0 + i]; occ[i] = host_occ[(uint16_t)y * grid_w + x0 + i]; }
}
