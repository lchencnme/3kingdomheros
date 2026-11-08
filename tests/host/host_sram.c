/* Host SRAM model with power-cut injection: after 'cut_after' byte writes all further writes are lost. */
#include <string.h>
#include "../../src/core/save.h"

uint8_t host_sram[16][8192];
long host_cut_after = -1;      /* -1 = never */
long host_writes = 0;

void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API
{
    uint16_t i;
    for (i = 0; i < n; i++) {
        if (host_cut_after >= 0 && host_writes >= host_cut_after) return;
        host_sram[bank][off + i] = src[i];
        host_writes++;
    }
}

void sram_read(uint8_t bank, uint16_t off, uint8_t *dst, uint16_t n) PLATFORM_API
{
    memcpy(dst, &host_sram[bank][off], n);
}
