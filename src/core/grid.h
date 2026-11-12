/* Platform-provided battle grid accessors (design doc 11.4).
 * GB: terrain in WRAM bank2, side occupancy bitmaps in bank7, field in banks3/4,
 *     queue in banks5/6. Each accessor must save/restore SVBK itself.
 * Host tests: plain arrays (tests/host/host_grid.c). */
#ifndef CORE_GRID_H
#define CORE_GRID_H
#include <stdint.h>
#include "platform.h"

#define SIDE_A 1u   /* PLAYER + ALLY */
#define SIDE_E 2u   /* ENEMY */

extern uint8_t grid_w, grid_h;
/* 2 x 64-byte row buffers in non-banked RAM (GB: UI arena tail 0xCB60..0xCBDF, design doc 11.4; 0xCBE0..0xCBF7 mapview order/cand) */
#ifdef __SDCC
#define grid_rowbuf_ter ((uint8_t *)0xCB60u)
#define grid_rowbuf_occ ((uint8_t *)0xCBA0u)
#else
extern uint8_t grid_rowbuf_ter[64], grid_rowbuf_occ[64];
#endif

uint8_t grid_terrain(uint8_t x, uint8_t y) PLATFORM_API;   /* low nibble: effective terrain id */
uint8_t grid_occ(uint8_t x, uint8_t y) PLATFORM_API;       /* 0, SIDE_A or SIDE_E */
/* bulk read of one row segment (x0..x0+n-1 inside the map) into caller buffers in fixed RAM:
   one bank switch per row instead of per cell */
void grid_row(uint8_t y, uint8_t x0, uint8_t n, uint8_t *ter, uint8_t *occ) PLATFORM_API;

/* full-map approach field (design doc 4.2, 11.4): one 2-byte record per cell
 *   [0] dist u8 (255 = unreached, saturates at 254)   [1] entry cost (FC_BLOCK = cannot stand/enter) | FC_INQ
 * GB: cells in WRAM banks 3-4, ring queue of (x,y) byte pairs in banks 5-6. fc_ptr/fq_ptr SWITCH SVBK
 * and return a pointer valid until the next switch; callers save/restore SVBK around the whole job. */
#define FC_INQ 0x80u
#define FC_BLOCK 0x7Fu
#define FC_UNREACHED 255u
#define FC_SATURATE 254u
#ifdef __SDCC
#include <gb/gb.h>
static inline uint8_t *fc_ptr(uint16_t i)
{
    SVBK_REG = (uint8_t)(3u + ((uint8_t)(i >> 8) >> 3));
    return (uint8_t *)(0xD000u + ((i & 0x7FFu) << 1));
}
static inline uint8_t *fq_ptr(uint16_t k)
{
    SVBK_REG = (uint8_t)(5u + ((uint8_t)(k >> 8) >> 3));
    return (uint8_t *)(0xD000u + ((k & 0x7FFu) << 1));
}
#else
uint8_t *fc_ptr(uint16_t i);
uint8_t *fq_ptr(uint16_t k);
#endif
/* field distance of cell i (FIELD_INF when unreached); saves/restores SVBK */
uint16_t fld_get(uint16_t i) PLATFORM_API;
#endif
