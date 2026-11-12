#ifdef __SDCC
#pragma bank 13
#endif
#include "undo.h"
#include "save.h"

static uint8_t active, overflow, rolling;
static uint16_t pos;

void undo_begin(void) CORE_API { active = 1; overflow = 0; pos = 0; }
uint8_t undo_active(void) CORE_API { return (uint8_t)(active && !rolling); }
uint8_t undo_overflowed(void) CORE_API { return overflow; }
uint16_t undo_used(void) CORE_API { return pos; }
void undo_end(void) CORE_API { active = 0; pos = 0; }

uint8_t undo_log(uint8_t space, uint8_t bank, uint16_t off, uint8_t len, const uint8_t *old) CORE_API
{
    uint8_t h[5], t[2];
    uint16_t size = (uint16_t)(7u + len);
    if (!active || rolling) return 1;
    if (overflow || (uint32_t)pos + size > UNDO_CAP) { overflow = 1; return 0; }   /* reserve first */
    h[0] = space; h[1] = bank; h[2] = (uint8_t)off; h[3] = (uint8_t)(off >> 8); h[4] = len;
    t[0] = (uint8_t)size; t[1] = (uint8_t)(size >> 8);
    sram_write(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + pos), h, 5);
    sram_write(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + pos + 5u), old, len);
    sram_write(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + pos + 5u + len), t, 2);
    pos = (uint16_t)(pos + size);
    return 1;
}

void undo_rollback(void) CORE_API
{
    uint8_t h[5], t[2], data[64];
    if (!active) return;
    rolling = 1;
    while (pos >= 7u) {
        uint16_t size, start;
        uint8_t len, done = 0;
        sram_read(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + pos - 2u), t, 2);
        size = (uint16_t)(t[0] | ((uint16_t)t[1] << 8));
        if (size < 7u || size > pos) break;                 /* corrupt: stop (never write garbage) */
        start = (uint16_t)(pos - size);
        sram_read(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + start), h, 5);
        len = h[4];
        while (done < len) {                                /* restore in pieces of <= 64 B */
            uint8_t k = (uint8_t)(len - done) < 64u ? (uint8_t)(len - done) : 64u;
            sram_read(UNDO_SRAM_BANK, (uint16_t)(UNDO_SRAM_OFF + start + 5u + done), data, k);
            undo_restore(h[0], h[1], (uint16_t)((h[2] | ((uint16_t)h[3] << 8)) + done), data, k);
            done = (uint8_t)(done + k);
        }
        pos = start;
    }
    undo_rebuild();
    rolling = 0;
    active = 0;
    pos = 0;
}
