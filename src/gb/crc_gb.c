/* SM83 CRC32 inner loop (design doc 11.6 performance): byte-sliced tables, page-aligned in WRAM bank 6
 * (D000/D100/D200/D300, copied from ROM at the start of each save/load), state bytes in HRAM FFF0..FFF3.
 * ~35 M-cycles per byte versus ~150 for the SDCC-compiled C loop. Data must be in fixed RAM. */
#include <gb/gb.h>
#include <stdint.h>
#include "rom.h"

#define CRC_TAB_WBANK 6u
uint16_t crc_ptr;
uint8_t crc_len;
volatile __at(0xFFF0) uint8_t crc_c0;
volatile __at(0xFFF1) uint8_t crc_c1;
volatile __at(0xFFF2) uint8_t crc_c2;
volatile __at(0xFFF3) uint8_t crc_c3;

static void crc_block_asm(void) NONBANKED __naked
{
    __asm
    ld hl, #_crc_ptr
    ld e, (hl)
    inc hl
    ld d, (hl)
    ld a, (#_crc_len)
    ld b, a
1$:
    ld a, (de)
    inc de
    ld c, a
    ldh a, (0xF0)
    xor a, c
    ld l, a
    ld h, #0xD0
    ld a, (hl)
    ld c, a
    ldh a, (0xF1)
    xor a, c
    ldh (0xF0), a
    inc h
    ld a, (hl)
    ld c, a
    ldh a, (0xF2)
    xor a, c
    ldh (0xF1), a
    inc h
    ld a, (hl)
    ld c, a
    ldh a, (0xF3)
    xor a, c
    ldh (0xF2), a
    inc h
    ld a, (hl)
    ldh (0xF3), a
    dec b
    jr nz, 1$
    ret
    __endasm;
}

/* tables: 4 x 256 B from the core's ROM copy (bank of save.c) */
extern const uint8_t crc_t0[256];
#define CRC_ROM_BANK 4u

void crc_prepare(void) NONBANKED
{
    uint8_t o = SVBK_REG;
    SVBK_REG = CRC_TAB_WBANK;
    rom_copy(CRC_ROM_BANK, (void *)0xD000u, crc_t0, 1024);   /* t0..t3 are contiguous in save.c */
    SVBK_REG = o;
}

uint32_t crc_update_gb(uint32_t crc, const uint8_t *p, uint16_t n) NONBANKED
{
    uint8_t o = SVBK_REG;
    crc_c0 = (uint8_t)crc; crc_c1 = (uint8_t)(crc >> 8); crc_c2 = (uint8_t)(crc >> 16); crc_c3 = (uint8_t)(crc >> 24);
    SVBK_REG = CRC_TAB_WBANK;
    while (n) {
        uint8_t k = n > 255u ? 255u : (uint8_t)n;
        crc_ptr = (uint16_t)p; crc_len = k;
        crc_block_asm();
        p += k; n -= k;
    }
    SVBK_REG = o;
    return (uint32_t)crc_c0 | ((uint32_t)crc_c1 << 8) | ((uint32_t)crc_c2 << 16) | ((uint32_t)crc_c3 << 24);
}
