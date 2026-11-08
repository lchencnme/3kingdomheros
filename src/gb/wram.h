/* Banked WRAM layout (design doc 11.4) and accessors. Every accessor saves/restores SVBK. */
#ifndef GB_WRAM_H
#define GB_WRAM_H
#include <stdint.h>
#include "../core/path.h"

#define WB_UNITS    1u   /* D000 active units 96x40, DF00 campaign area */
#define WB_TERRAIN  2u   /* D000 terrain 4096 */
#define WB_FIELD    3u   /* banks 3-4: approach field u16 x 4096 */
#define WB_QUEUE    5u   /* banks 5-6: SPFA ring queue u16 x 4096 */
#define WB_SCRATCH  7u   /* see below */

#define W7_OCC_A   ((uint8_t *)0xD000)
#define W7_OCC_E   ((uint8_t *)0xD200)
#define W7_INQ     ((uint8_t *)0xD400)   /* full-map inqueue bits / danger view (exclusive) */
#define W7_MR      ((MoveRange *)0xD600) /* local move range, D600..DBFF (1536B) */
typedef char mr_fits_bank7_region[(sizeof(MoveRange) <= 0x600u) ? 1 : -1];
#define W2_TERRAIN ((uint8_t *)0xD000)

void wram_init_grid(uint8_t w, uint8_t h, const uint8_t *terrain);
void occ_set(uint8_t x, uint8_t y, uint8_t side);
void occ_clear(uint8_t x, uint8_t y);
#endif
