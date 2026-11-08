/* SRAM access hooks (design doc 11.5): RAM is enabled only for the duration of one call and the
 * bank is parked on 12 (runtime work area) before disabling, so a stray write can never hit a save copy. */
#include <gb/gb.h>
#include <string.h>
#include "../core/save.h"

#define SRAM_PARK_BANK 12u

void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API
{
    ENABLE_RAM;
    SWITCH_RAM(bank);
    memcpy((uint8_t *)0xA000u + off, src, n);
    SWITCH_RAM(SRAM_PARK_BANK);
    DISABLE_RAM;
}

void sram_read(uint8_t bank, uint16_t off, uint8_t *dst, uint16_t n) PLATFORM_API
{
    ENABLE_RAM;
    SWITCH_RAM(bank);
    memcpy(dst, (const uint8_t *)0xA000u + off, n);
    SWITCH_RAM(SRAM_PARK_BANK);
    DISABLE_RAM;
}
