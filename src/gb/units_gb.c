/* GB storage for battle units: 40B records in WRAM bank1 at D000 (design doc 11.4). */
#include <gb/gb.h>
#include <string.h>
#include "../core/battle.h"
#include "../core/roster.h"
#include "../core/undo.h"
#include "../core/save.h"
void undo_restore_core(uint16_t off, const uint8_t *data, uint8_t len);
#include "wram.h"
#define SNAP_MAX 12u

#define W1_UNITS ((ActiveUnit *)0xD000)

void unit_load(uint8_t i, ActiveUnit *u) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memcpy(u, &W1_UNITS[i], sizeof(ActiveUnit));
    SVBK_REG = o;
}

uint8_t unit_gen;          /* bumped on every unit write: lets the renderer skip unchanged frames */

/* own frame: unit_store is at the bottom of the deepest chains */
static uint8_t log_unit(uint8_t i)
{
    ActiveUnit old;
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS; memcpy(&old, &W1_UNITS[i], sizeof old); SVBK_REG = o;
    return undo_log(US_UNITS, 1, (uint16_t)((uint16_t)i * sizeof(ActiveUnit)), sizeof(ActiveUnit), (const uint8_t *)&old);
}

void unit_store(uint8_t i, const ActiveUnit *u) PLATFORM_API
{
    unit_gen++;
    if (undo_active() && !log_unit(i)) return;   /* 11.6: script transaction -> log the old record */
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memcpy(&W1_UNITS[i], u, sizeof(ActiveUnit));
    SVBK_REG = o;
}

/* TECH01 templates: every unit 70/60/70 (roster + ROM templates come with the campaign module) */
void unit_base(uint8_t i, UnitBase *b) PLATFORM_API
{
    /* officers: base stats from their roster record (B.2); everyone else: template defaults */
    uint16_t ro;
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    ro = W1_UNITS[i].roster_offset;
    SVBK_REG = o;
    if (ro != NO_ROSTER && ro < 8u * ROSTER_REC) {
        /* officer: cached copy of the record's STR/INT/CMD in WRAM7 D4E8 (8 x 3 B; str 0 = not cached).
         * The AI asks for base stats in its inner loops; the SRAM read + bank call is too slow there. */
        uint8_t k = (uint8_t)(ro / ROSTER_REC), *c;
        SVBK_REG = 7;
        c = (uint8_t *)0xD4E8u + k * 3u;
        if (c[0]) { b->str = c[0]; b->intl = c[1]; b->cmd = c[2]; SVBK_REG = o; return; }
        SVBK_REG = o;
        if (roster_base(ro, b)) {
            SVBK_REG = 7; c[0] = b->str; c[1] = b->intl; c[2] = b->cmd; SVBK_REG = o;
            return;
        }
    } else if (ro != NO_ROSTER && roster_base(ro, b)) return;
    b->str = 70; b->intl = 60; b->cmd = 70;
}

void unit_base_cache_clear(void)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = 7; memset((uint8_t *)0xD4E8u, 0, 24); SVBK_REG = o;
}

void grid_occ_set(uint8_t x, uint8_t y, uint8_t side) PLATFORM_API { occ_set(x, y, side); }
void grid_occ_clear(uint8_t x, uint8_t y) PLATFORM_API { occ_clear(x, y); }

void units_clear(void)
{
    unit_gen++;
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS;
    memset(W1_UNITS, 0, 96u * sizeof(ActiveUnit));
    SVBK_REG = o;
}

/* Per-frame render/UI snapshot of the first n units (temporary OBJ placeholders need it every frame).
 * One SVBK switch, direct field reads: ~20x cheaper than n unit_load() copies. */
uint8_t snap_x[SNAP_MAX], snap_y[SNAP_MAX], snap_fac[SNAP_MAX], snap_flags[SNAP_MAX], snap_cls[SNAP_MAX];
uint16_t snap_hp[SNAP_MAX];

void units_snapshot(uint8_t n)
{
    uint8_t o = SVBK_REG, i;
    const ActiveUnit *u = W1_UNITS;
    SVBK_REG = WB_UNITS;
    if (n > SNAP_MAX) n = SNAP_MAX;
    for (i = 0; i < n; i++, u++) {
        snap_x[i] = u->x; snap_y[i] = u->y; snap_fac[i] = u->faction;
        snap_flags[i] = u->action_flags; snap_hp[i] = u->hp; snap_cls[i] = u->class_id;
    }
    SVBK_REG = o;
}

/* 11.6 rollback: raw restores (no logging) and the derived occupancy rebuilt from the units */
void undo_restore(uint8_t space, uint8_t bank, uint16_t off, const uint8_t *data, uint8_t len) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    (void)bank;
    if (space == US_UNITS) { SVBK_REG = WB_UNITS; memcpy((uint8_t *)W1_UNITS + off, data, len); SVBK_REG = o; unit_gen++; }
    else if (space == US_CAMPAIGN) campaign_write((uint8_t)off, data, len);
    else if (space == US_CORE) undo_restore_core(off, data, len);
    else sram_write(bank, off, data, len);     /* undo_active() is 0 while rolling back */
}

void undo_rebuild(void) PLATFORM_API
{
    uint8_t i, o = SVBK_REG;
    ActiveUnit u;
    SVBK_REG = WB_SCRATCH;
    memset((uint8_t *)0xD000u, 0, 0x400u);     /* both occupancy bitmaps (D000 / D200) */
    SVBK_REG = o;
    for (i = 0; i < MAX_UNITS; i++) {
        unit_load(i, &u);
        if (u.max_hp && unit_alive(&u)) occ_set(u.x, u.y, faction_side(u.faction));
    }
    unit_gen++;
}
