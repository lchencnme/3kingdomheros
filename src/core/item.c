#ifdef __SDCC
#pragma bank 12
#endif
#include <string.h>
#include "item.h"
#include "grid.h"
#include "path.h"
#include "save.h"

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

static uint16_t rec_off(uint8_t unit, uint8_t slot) { return (uint16_t)((uint16_t)unit * INV_REC + (uint16_t)slot * 2u); }

uint8_t inv_get(uint8_t unit, uint8_t slot) CORE_API
{
    uint8_t v[2];
    if (unit >= MAX_UNITS || slot >= INV_SLOTS) return ITEM_NONE;
    sram_read(INV_SRAM_BANK, rec_off(unit, slot), v, 2);
    if (v[1] != 0 || v[0] >= NUM_ITEMS) return ITEM_NONE;          /* FFFF (or unknown id) = empty */
    return v[0];
}

void inv_set(uint8_t unit, uint8_t slot, uint8_t item) CORE_API
{
    uint8_t v[2];
    if (unit >= MAX_UNITS || slot >= INV_SLOTS) return;
    if (item == ITEM_NONE) { v[0] = 0xFF; v[1] = 0xFF; } else { v[0] = item; v[1] = 0; }
    sram_write(INV_SRAM_BANK, rec_off(unit, slot), v, 2);
}

uint8_t inv_add(uint8_t unit, uint8_t item) CORE_API
{
    uint8_t s;
    for (s = 0; s < INV_SLOTS; s++)
        if (inv_get(unit, s) == ITEM_NONE) { inv_set(unit, s, item); return s; }
    return ITEM_NONE;
}

uint8_t inv_count(uint8_t unit) CORE_API
{
    uint8_t s, n = 0;
    for (s = 0; s < INV_SLOTS; s++) if (inv_get(unit, s) != ITEM_NONE) n++;
    return n;
}

void inv_clear_all(void) CORE_API
{
    uint8_t ff[32];
    uint16_t k;
    memset(ff, 0xFF, sizeof ff);
    for (k = 0; k < (uint16_t)MAX_UNITS * INV_REC; k += 32u) sram_write(INV_SRAM_BANK, k, ff, 32);
}

static uint8_t unit_at(const Battle *b, uint8_t x, uint8_t y, ActiveUnit *u)
{
    uint8_t i;
    for (i = 0; i < b->n_units; i++) {
        unit_load(i, u);
        if (unit_alive(u) && u->x == x && u->y == y) return i;
    }
    return 0xFF;
}

uint16_t item_value(const Battle *b, uint8_t item, const ActiveUnit *t) CORE_API
{
    const ItemDef *d;
    if (item >= NUM_ITEMS) return 0;
    d = &item_defs[item];
    switch (d->kind) {
    case IK_HP: { uint16_t gap = (uint16_t)(t->max_hp - t->hp); return d->power < gap ? d->power : gap; }
    case IK_MORALE: { uint8_t gap = (uint8_t)(t->morale < 100u ? 100u - t->morale : 0u); return d->power < gap ? d->power : gap; }
    case IK_MP: { uint16_t gap = (uint16_t)(t->max_mp - t->mp); return d->power < gap ? d->power : gap; }
    case IK_CLEANSE: return t->status_mask ? 1u : 0u;
    case IK_FIRE: return d->power < t->hp ? d->power : t->hp;
    case IK_RAIN: return b->weather ? 0u : 1u;
    case IK_CLEAR: return b->weather ? 1u : 0u;
    }
    return 0;
}

uint8_t item_check(const Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty) CORE_API
{
    ActiveUnit c, t;
    uint8_t it = inv_get(user, slot), ti, d;
    const ItemDef *def;
    if (it == ITEM_NONE) return IR_EMPTY;
    def = &item_defs[it];
    unit_load(user, &c);
    if (tx >= grid_w || ty >= grid_h) return IR_RANGE;
    d = manhattan(c.x, c.y, tx, ty);
    if (d > def->range) return IR_RANGE;
    if (def->kind == IK_RAIN || def->kind == IK_CLEAR)
        return item_value(b, it, &c) ? IR_OK : IR_NO_EFFECT;     /* self only (range 0) */
    if (def->kind == IK_FIRE && d > 1 && !los_clear(c.x, c.y, tx, ty)) return IR_RANGE;
    ti = unit_at(b, tx, ty, &t);
    if (ti == 0xFF) return IR_NO_TARGET;
    if ((def->kind == IK_FIRE) != factions_hostile(c.faction, t.faction)) return IR_NO_TARGET;
    if (def->kind == IK_FIRE && ti == user) return IR_NO_TARGET;
    return item_value(b, it, &t) ? IR_OK : IR_NO_EFFECT;
}

uint8_t item_use(Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty, ItemResult *r) CORE_API
{
    ActiveUnit c, t;
    uint8_t chk = item_check(b, user, slot, tx, ty), it, ti, k;
    const ItemDef *def;
    r->item = ITEM_NONE; r->target = 0xFF; r->killed = 0; r->amount = 0;
    if (chk != IR_OK) return chk;
    it = inv_get(user, slot);
    def = &item_defs[it];
    r->item = it;
    inv_set(user, slot, ITEM_NONE);                      /* consumed first: a reset never duplicates it */
    if (def->kind == IK_RAIN || def->kind == IK_CLEAR) {
        b->weather = def->kind == IK_RAIN ? 1u : 0u;
        r->target = user; r->amount = 1;
    } else {
        ti = unit_at(b, tx, ty, &t);
        r->target = ti;
        r->amount = item_value(b, it, &t);
        switch (def->kind) {
        case IK_HP: t.hp = (uint16_t)(t.hp + r->amount); break;
        case IK_MORALE: t.morale = (uint8_t)(t.morale + r->amount); break;
        case IK_MP: t.mp = (uint16_t)(t.mp + r->amount); break;
        case IK_CLEANSE:
            t.status_mask = 0; t.expiry_eligible_mask = 0;
            for (k = 0; k < 5; k++) t.ttl[k] = 0;
            break;
        case IK_FIRE:                                    /* fixed damage, always hits, no counter, 0 draws */
            if (r->amount >= t.hp) {
                grid_occ_clear(t.x, t.y);
                t.hp = 0; t.action_flags |= AF_RETREAT; t.status_mask = 0;
                r->killed = 1;
            } else t.hp = (uint16_t)(t.hp - r->amount);
            break;
        }
        unit_store(ti, &t);
    }
    unit_load(user, &c);
    if (unit_alive(&c)) { c.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED); unit_store(user, &c); }
    battle_check_outcome(b, 0);
    return IR_OK;
}

uint8_t item_exchange(uint8_t actor, uint8_t sa, uint8_t recv, uint8_t sb) CORE_API
{
    ActiveUnit a, r;
    uint8_t ia, ib;
    unit_load(actor, &a);
    unit_load(recv, &r);
    if (actor == recv || !unit_alive(&a) || !unit_alive(&r) || factions_hostile(a.faction, r.faction)) return IR_NO_TARGET;
    if (manhattan(a.x, a.y, r.x, r.y) != 1) return IR_RANGE;
    if (sa >= INV_SLOTS || sb >= INV_SLOTS) return IR_EMPTY;
    ia = inv_get(actor, sa); ib = inv_get(recv, sb);
    if (ia == ITEM_NONE && ib == ITEM_NONE) return IR_EMPTY;
    inv_set(recv, sb, ia);
    inv_set(actor, sa, ib);
    a.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);
    unit_store(actor, &a);
    return IR_OK;
}
