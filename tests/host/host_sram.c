/* Host SRAM model with power-cut injection: after 'cut_after' byte writes all further writes are lost. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/core/save.h"
#include "../../src/core/sram_map.h"

uint8_t host_sram[SRAM_BANKS][8192];   /* the cart: 32 KiB */
long host_cut_after = -1;      /* -1 = never */
long host_writes = 0;

#include "../../src/core/undo.h"
/* weak no-op undo hooks for tests that do not link undo.c */
__attribute__((weak)) uint8_t undo_active(void) { return 0; }
__attribute__((weak)) uint8_t undo_log(uint8_t s, uint8_t b, uint16_t o, uint8_t l, const uint8_t *d) { (void)s; (void)b; (void)o; (void)l; (void)d; return 1; }
__attribute__((weak)) void undo_begin(void) {}
__attribute__((weak)) void undo_end(void) {}
__attribute__((weak)) void undo_rollback(void) {}
__attribute__((weak)) uint8_t undo_overflowed(void) { return 0; }

void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API
{
    if (bank >= SRAM_BANKS || (uint32_t)off + n > 8192u) { fprintf(stderr, "sram_write out of the cart: bank %u off %u\n", bank, off); abort(); }
    if (bank == RT_BANK && off < RT_UNDO_OFF && undo_active()) {
        uint8_t old[32];
        uint16_t k;
        for (k = 0; k < n; k += 32u) {
            uint8_t m = (uint8_t)((n - k) < 32u ? (n - k) : 32u);
            memcpy(old, &host_sram[bank][off + k], m);
            if (!undo_log(off >= RT_INV_OFF ? US_SRAM15 : US_SRAM12, bank, (uint16_t)(off + k), m, old)) return;
        }
    }
    uint16_t i;
    for (i = 0; i < n; i++) {
        if (host_cut_after >= 0 && host_writes >= host_cut_after) return;
        host_sram[bank][off + i] = src[i];
        host_writes++;
    }
}

void sram_read(uint8_t bank, uint16_t off, uint8_t *dst, uint16_t n) PLATFORM_API
{
    if (bank >= SRAM_BANKS || (uint32_t)off + n > 8192u) { fprintf(stderr, "sram_read out of the cart: bank %u off %u\n", bank, off); abort(); }
    memcpy(dst, &host_sram[bank][off], n);
}

/* treasures (design doc 7.4) for host tests: set host_n_treasures and fill host_treasures */
#include "../../src/core/item.h"
TreasureDef host_treasures[8];
uint8_t host_n_treasures;
uint8_t treasure_def(uint8_t i, TreasureDef *t) PLATFORM_API
{
    if (i >= host_n_treasures) return 0;
    *t = host_treasures[i];
    return 1;
}
/* default VM flag storage for tests that do not run the VM (test_vm has its own strong versions) */
static uint8_t host_flags[2][512];
__attribute__((weak)) uint8_t vm_flag_get(uint8_t s, uint16_t id) PLATFORM_API { return (host_flags[s & 1][(id >> 3) & 511] >> (id & 7)) & 1; }
__attribute__((weak)) void vm_flag_set(uint8_t s, uint16_t id, uint8_t v) PLATFORM_API
{
    uint8_t *f = &host_flags[s & 1][(id >> 3) & 511], m = (uint8_t)(1u << (id & 7));
    *f = v ? (uint8_t)(*f | m) : (uint8_t)(*f & ~m);
}
/* campaign area (GB: WRAM1 DF00) */
uint8_t host_campaign[256];
void campaign_read(uint8_t off, uint8_t *dst, uint8_t n) PLATFORM_API { memcpy(dst, host_campaign + off, n); }
void campaign_write(uint8_t off, const uint8_t *src, uint8_t n) PLATFORM_API { memcpy(host_campaign + off, src, n); }
