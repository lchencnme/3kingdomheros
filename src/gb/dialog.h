/* Dialogue mode (design doc 12.3): 64x64 portrait at (8,8), name line + 2 text lines below. */
#ifndef GB_DIALOG_H
#define GB_DIALOG_H
#include <stdint.h>
/* plays queued text entries from index i up to the next non-text entry (A = next page);
 * returns that index; leaves the LCD off if anything was shown */
uint8_t dialog_run_from(uint8_t i) BANKED;
/* battle result page: open (LCD off, seal glyph), compose hud lists 2/0/1, then wait -> J_A / J_SELECT */
void result_open(uint16_t seal_tx) BANKED;
uint8_t result_wait(uint8_t select_ok) BANKED;
#endif
