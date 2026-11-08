/* Platform-provided battle grid accessors (design doc 11.4).
 * GB: terrain in WRAM bank2, side occupancy bitmaps in bank7, field in banks3/4,
 *     queue in banks5/6. Each accessor must save/restore SVBK itself.
 * Host tests: plain arrays (tests/host/host_grid.c). */
#ifndef CORE_GRID_H
#define CORE_GRID_H
#include <stdint.h>

#define SIDE_A 1u   /* PLAYER + ALLY */
#define SIDE_E 2u   /* ENEMY */

extern uint8_t grid_w, grid_h;

uint8_t grid_terrain(uint8_t x, uint8_t y);   /* low nibble: effective terrain id */
uint8_t grid_occ(uint8_t x, uint8_t y);       /* 0, SIDE_A or SIDE_E */
/* bulk read of one row segment (x0..x0+n-1 inside the map) into caller buffers in fixed RAM:
   one bank switch per row instead of per cell */
void grid_row(uint8_t y, uint8_t x0, uint8_t n, uint8_t *ter, uint8_t *occ);

/* full-map approach field (u16 x 4096) and its SPFA ring queue (u16 x 4096) + inqueue bits */
uint16_t fld_get(uint16_t i);
void fld_set(uint16_t i, uint16_t v);
uint16_t fq_get(uint16_t i);
void fq_set(uint16_t i, uint16_t v);
uint8_t fin_test(uint16_t i);
void fin_set(uint16_t i);
void fin_clear(uint16_t i);
#endif
