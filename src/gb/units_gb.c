/* GB storage for battle units: 40B records in WRAM bank1 at D000 (design doc 11.4). */
#include <gb/gb.h>
#include <string.h>
#include "../core/battle.h"
#include "wram.h"
#define SNAP_MAX 10u

#define W1_UNITS ((ActiveUnit *)0xD000)

void unit_load(uint8_t i, ActiveUnit *u) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memcpy(u, &W1_UNITS[i], sizeof(ActiveUnit));
    SVBK_REG = o;
}

void unit_store(uint8_t i, const ActiveUnit *u) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memcpy(&W1_UNITS[i], u, sizeof(ActiveUnit));
    SVBK_REG = o;
}

/* TECH01 templates: every unit 70/60/70 (roster + ROM templates come with the campaign module) */
void unit_base(uint8_t i, UnitBase *b) PLATFORM_API
{
    (void)i;
    b->str = 70; b->intl = 60; b->cmd = 70;
}

void grid_occ_set(uint8_t x, uint8_t y, uint8_t side) PLATFORM_API { occ_set(x, y, side); }
void grid_occ_clear(uint8_t x, uint8_t y) PLATFORM_API { occ_clear(x, y); }

void units_clear(void)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memset(W1_UNITS, 0, 96u * sizeof(ActiveUnit));
    SVBK_REG = o;
}

/* Per-frame render/UI snapshot of the first n units (temporary OBJ placeholders need it every frame).
 * One SVBK switch, direct field reads: ~20x cheaper than n unit_load() copies. */
uint8_t snap_x[SNAP_MAX], snap_y[SNAP_MAX], snap_fac[SNAP_MAX], snap_flags[SNAP_MAX];
uint16_t snap_hp[SNAP_MAX];

void units_snapshot(uint8_t n)
{
    uint8_t o = SVBK_REG, i;
    const ActiveUnit *u = W1_UNITS;
    SVBK_REG = WB_UNITS;
    if (n > SNAP_MAX) n = SNAP_MAX;
    for (i = 0; i < n; i++, u++) {
        snap_x[i] = u->x; snap_y[i] = u->y; snap_fac[i] = u->faction;
        snap_flags[i] = u->action_flags; snap_hp[i] = u->hp;
    }
    SVBK_REG = o;
}
