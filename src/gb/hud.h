/* Bottom HUD (design doc 10.4, 12.2): Window 160x32 at WY=112, two lines of 12px text from x=8.
 * Double-buffered in VRAM bank1 (page A ids 0..71 at $9000, page B ids 128..199 at $8800). */
#ifndef GB_HUD_H
#define GB_HUD_H
#include <stdint.h>

void hud_init(uint8_t blank_tile);
void hud_begin(void);                          /* start composing the back page */
void hud_tx(uint8_t line, uint16_t tx, uint16_t var);   /* append text (TX_*), {n} -> var */
void hud_num(uint8_t line, uint16_t n);
void hud_space(uint8_t line);
void hud_glyph(uint8_t line, uint16_t g);
void hud_end(void);                            /* schedule: raster + GDMA + flip over the next frames */
void hud_service(void);                        /* main loop, visible period */
void hud_commit(void);                         /* inside VBlank */
/* rasterise one composed line into the 576 B staging buffer at 0xC800 (2 tile rows x 18 tiles) */
void hud_raster(uint8_t line);
uint8_t hud_tx_page(uint16_t tx, uint8_t page);   /* page of a TXD_* string into lists 0/1 */
void hud_tx_list(uint8_t list, uint16_t tx, uint16_t var, uint8_t k);
void hud_raster_list(uint8_t list);
#endif
