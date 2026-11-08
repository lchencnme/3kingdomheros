#include <string.h>
#include "rom.h"

void rom_copy(uint8_t bank, void *dst, const void *src, uint16_t n) NONBANKED
{
    uint8_t save = _current_bank;
    SWITCH_ROM(bank);
    memcpy(dst, src, n);
    SWITCH_ROM(save);
}

void rom_set_bkg_data(uint8_t bank, uint8_t first, uint8_t n, const uint8_t *src) NONBANKED
{
    uint8_t save = _current_bank;
    SWITCH_ROM(bank);
    set_bkg_data(first, n, src);
    SWITCH_ROM(save);
}

void rom_set_sprite_data(uint8_t bank, uint8_t first, uint8_t n, const uint8_t *src) NONBANKED
{
    uint8_t save = _current_bank;
    SWITCH_ROM(bank);
    set_sprite_data(first, n, src);
    SWITCH_ROM(save);
}
