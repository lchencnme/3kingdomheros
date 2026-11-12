/* SRAM access hooks (design doc 11.5): RAM is enabled only for the duration of one call and the
 * bank is parked on 12 (runtime work area) before disabling, so a stray write can never hit a save copy. */
#include <gb/gb.h>
#include <string.h>
#include "../core/save.h"
#include "../core/undo.h"

#define SRAM_PARK_BANK 12u

/* 11.6: inside a script transaction, log the old bytes first (own frame: sram_write is everywhere) */
static uint8_t log_old(uint8_t bank, uint16_t off, uint16_t n)
{
    uint8_t old[32];
    uint16_t k;
    for (k = 0; k < n; k += 32u) {
        uint8_t m = (uint8_t)((n - k) < 32u ? (n - k) : 32u);
        sram_read(bank, (uint16_t)(off + k), old, m);
        if (!undo_log(bank == 12u ? US_SRAM12 : US_SRAM15, bank, (uint16_t)(off + k), m, old)) return 0;
    }
    return 1;
}

void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API
{
    if ((bank == 12u || (bank == 15u && off < 1920u)) && undo_active() && !log_old(bank, off, n)) return;   /* no room: no write */
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
