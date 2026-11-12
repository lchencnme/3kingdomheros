/* Host implementation of unit storage hooks. */
#include <string.h>
#include "../../src/core/battle.h"
#include "../../src/core/grid.h"
#include "../../src/core/undo.h"

extern uint8_t host_occ[4096];
ActiveUnit host_units[MAX_UNITS];
UnitBase host_base[MAX_UNITS];

void unit_load(uint8_t i, ActiveUnit *u) PLATFORM_API { *u = host_units[i]; }
void unit_store(uint8_t i, const ActiveUnit *u) PLATFORM_API
{
    if (undo_active() && !undo_log(US_UNITS, 1, (uint16_t)(i * sizeof(ActiveUnit)), sizeof(ActiveUnit), (const uint8_t *)&host_units[i])) return;
    host_units[i] = *u;
}

void unit_xy(uint8_t i, uint8_t *x, uint8_t *y) PLATFORM_API { *x = host_units[i].x; *y = host_units[i].y; }

uint8_t unit_state_bits(uint8_t i) PLATFORM_API
{
    const ActiveUnit *u = &host_units[i];
    return (uint8_t)((unit_alive(u) ? USB_ALIVE : 0u) | (u->max_hp ? USB_SPAWNED : 0u) | (u->faction == FAC_ENEMY ? USB_ENEMY : 0u));
}

/* 11.6 rollback hooks */
void campaign_write(uint8_t off, const uint8_t *src, uint8_t n) PLATFORM_API;
void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API;
uint16_t host_gold;
void undo_restore(uint8_t space, uint8_t bank, uint16_t off, const uint8_t *data, uint8_t len) PLATFORM_API
{
    if (space == US_UNITS) memcpy((uint8_t *)host_units + off, data, len);
    else if (space == US_CAMPAIGN) campaign_write((uint8_t)off, data, len);
    else if (space == US_CORE) memcpy(&host_gold, data, len);
    else sram_write(bank, off, data, len);
}
void undo_rebuild(void) PLATFORM_API
{
    uint8_t i;
    memset(host_occ, 0, 4096);
    for (i = 0; i < MAX_UNITS; i++)
        if (host_units[i].max_hp && unit_alive(&host_units[i])) grid_occ_set(host_units[i].x, host_units[i].y, faction_side(host_units[i].faction));
}
/* officers on the roster take their base stats from the record (roster.c overrides this weak default) */
__attribute__((weak)) uint8_t roster_base(uint16_t roster_offset, UnitBase *b) { (void)roster_offset; (void)b; return 0; }
void unit_base(uint8_t i, UnitBase *b) PLATFORM_API
{
    if (host_units[i].roster_offset != NO_ROSTER && roster_base(host_units[i].roster_offset, b)) return;
    *b = host_base[i];
}
void grid_occ_set(uint8_t x, uint8_t y, uint8_t side) PLATFORM_API { host_occ[(uint16_t)y * grid_w + x] = side; }
void grid_occ_clear(uint8_t x, uint8_t y) PLATFORM_API { host_occ[(uint16_t)y * grid_w + x] = 0; }
