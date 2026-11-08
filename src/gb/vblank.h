/* VBlank-safe VRAM transfer helpers shared by fullscreen and dialogue modes (design doc 12.1).
 * All live in code bank 10 together with their callers. */
#ifndef GB_VBLANK_H
#define GB_VBLANK_H
#include <stdint.h>

void vb_wait(void);                                     /* next VBlank with LY in 144..149 */
uint8_t vb_gdma(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n);   /* atomic LY check + start */
void vb_gdma_wait(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n); /* retry on later VBlanks */
uint16_t vb_slot_addr(uint8_t id);                      /* BG tile id -> address (LCDC.4 = 0) */
#endif
