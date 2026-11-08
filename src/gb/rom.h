/* ROM bank gateway (design doc 11.3). Asset banks are only touched from NONBANKED code in bank 0;
 * every call restores the code bank (1) before returning. */
#ifndef GB_ROM_H
#define GB_ROM_H
#include <stdint.h>
#include <gb/gb.h>

#define CODE_BANK 1u
#define ASSET_BANK 2u

void rom_copy(uint8_t bank, void *dst, const void *src, uint16_t n) NONBANKED;
void rom_set_bkg_data(uint8_t bank, uint8_t first, uint8_t n, const uint8_t *src) NONBANKED;
void rom_set_sprite_data(uint8_t bank, uint8_t first, uint8_t n, const uint8_t *src) NONBANKED;
#endif
