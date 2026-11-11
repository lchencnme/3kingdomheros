#ifdef __SDCC
#pragma bank 13
#endif
#include <string.h>
#include "item.h"
#include "grid.h"
#include "path.h"
#include "save.h"
#include "vm.h"

#define EQ_OFF 16u            /* record: 8 x u16 items, then 3 equip slot indices (FF = none), 1 pad */

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

static uint16_t rec_off(uint8_t unit, uint8_t slot) { return (uint16_t)((uint16_t)unit * INV_REC + (uint16_t)slot * 2u); }
static uint16_t eq_off(uint8_t unit, uint8_t cat) { return (uint16_t)((uint16_t)unit * INV_REC + EQ_OFF + cat); }

static uint8_t item_known(uint8_t it) { return it < NUM_ITEMS && item_defs[it].kind != IK_NONE; }

uint8_t inv_get(uint8_t unit, uint8_t slot) CORE_API
{
    uint8_t v[2];
    if (unit >= MAX_UNITS || slot >= INV_SLOTS) return ITEM_NONE;
    sram_read(INV_SRAM_BANK, rec_off(unit, slot), v, 2);
    if (v[1] != 0 || !item_known(v[0])) return ITEM_NONE;          /* FFFF (or unknown id) = empty */
    return v[0];
}

uint8_t inv_equipped(uint8_t unit, uint8_t cat) CORE_API
{
    uint8_t s;
    if (unit >= MAX_UNITS || cat > EQ_MOUNT) return ITEM_NONE;
    sram_read(INV_SRAM_BANK, eq_off(unit, cat), &s, 1);
    return s < INV_SLOTS ? s : ITEM_NONE;
}

static void eq_write(uint8_t unit, uint8_t cat, uint8_t slot) { sram_write(INV_SRAM_BANK, eq_off(unit, cat), &slot, 1); }

uint8_t inv_slot_equipped(uint8_t unit, uint8_t slot) CORE_API
{
    uint8_t c;
    for (c = 0; c <= EQ_MOUNT; c++) if (inv_equipped(unit, c) == slot) return 1;
    return 0;
}

void inv_set(uint8_t unit, uint8_t slot, uint8_t item) CORE_API
{
    uint8_t v[2], c;
    if (unit >= MAX_UNITS || slot >= INV_SLOTS) return;
    for (c = 0; c <= EQ_MOUNT; c++)                                   /* the slot's content changes: */
        if (inv_equipped(unit, c) == slot) eq_write(unit, c, 0xFF);   /* it is no longer equipped */
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

/* ---------------------------------------------------------------- equipment (7.1) */
uint8_t item_category(uint8_t item) CORE_API
{
    if (!item_known(item)) return EQ_NONE;
    switch (item_defs[item].kind) {
    case IK_WEAPON: return EQ_WEAPON;
    case IK_MANUAL: return EQ_MANUAL;
    case IK_MOUNT: return EQ_MOUNT;
    }
    return EQ_NONE;
}

void item_mods(uint8_t unit, StatIn *s) CORE_API
{
    uint8_t c;
    for (c = 0; c <= EQ_MOUNT; c++) {
        uint8_t sl = inv_equipped(unit, c), it;
        const ItemDef *d;
        if (sl == ITEM_NONE) continue;
        it = inv_get(unit, sl);
        if (item_category(it) != c) continue;                          /* stale index: ignore */
        d = &item_defs[it];
        s->mod_atk += d->atk; s->mod_mag += d->mag; s->mod_res += d->res;
        s->mod_agi = (int8_t)(s->mod_agi + d->agi); s->mod_move = (int8_t)(s->mod_move + d->move);
    }
}

uint8_t item_heal_bonus(uint8_t unit) CORE_API
{
    uint8_t sl = inv_equipped(unit, EQ_MANUAL), it;
    if (sl == ITEM_NONE) return 0;
    it = inv_get(unit, sl);
    return item_category(it) == EQ_MANUAL ? item_defs[it].p1 : 0u;
}

void item_rederive(uint8_t unit) CORE_API
{
    ActiveUnit u;
    StatIn si;
    Derived d;
    UnitBase base;
    uint16_t old_mhp, old_mmp;
    unit_load(unit, &u);
    old_mhp = u.max_hp; old_mmp = u.max_mp;
    unit_base(unit, &base);
    si.cls = u.class_id; si.level = u.level; si.str = base.str; si.intl = base.intl; si.cmd = base.cmd;
    si.mod_hp = si.mod_mp = si.mod_atk = si.mod_def = si.mod_mag = si.mod_res = 0; si.mod_agi = 0; si.mod_move = 0;
    item_mods(unit, &si);
    rules_derive(&si, &d);
    if (old_mhp) {
        u.hp = rules_keep_deficit(old_mhp, u.hp, d.max_hp, u.hp ? 1u : 0u);
        u.mp = rules_keep_deficit(old_mmp, u.mp, d.max_mp, 0);
    } else { u.hp = d.max_hp; u.mp = d.max_mp; }
    u.max_hp = d.max_hp; u.max_mp = d.max_mp; u.atk = d.atk; u.def = d.def;
    u.mag = d.mag; u.res = d.res; u.agility = d.agi; u.move = d.move;
    unit_store(unit, &u);
}

uint8_t item_equip(uint8_t unit, uint8_t slot) CORE_API
{
    ActiveUnit u;
    uint8_t it = inv_get(unit, slot), cat = item_category(it);
    if (it == ITEM_NONE) return IR_EMPTY;
    if (cat == EQ_NONE) return IR_NO_EFFECT;
    unit_load(unit, &u);
    if (class_defs[u.class_id].family == FAM_CIVILIAN) return IR_CLASS;   /* 民众不能装备 (6.1) */
    eq_write(unit, cat, slot);
    item_rederive(unit);
    return IR_OK;
}

void item_unequip(uint8_t unit, uint8_t cat) CORE_API
{
    if (inv_equipped(unit, cat) == ITEM_NONE) return;
    eq_write(unit, cat, 0xFF);
    item_rederive(unit);
}

uint16_t item_sell_price(uint8_t item) CORE_API
{
    if (!item_known(item) || !(item_defs[item].flags & IF_SELL)) return 0;
    return (uint16_t)(item_defs[item].price / 2u);
}

/* ---------------------------------------------------------------- use (7.3) */
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
    if (!item_known(item)) return 0;
    d = &item_defs[item];
    switch (d->kind) {
    case IK_HP: { uint16_t gap = (uint16_t)(t->max_hp - t->hp); return d->power < gap ? d->power : gap; }
    case IK_MORALE: { uint8_t gap = (uint8_t)(t->morale < 100u ? 100u - t->morale : 0u); return d->power < gap ? d->power : gap; }
    case IK_MP: { uint16_t gap = (uint16_t)(t->max_mp - t->mp); return d->power < gap ? d->power : gap; }
    case IK_CLEANSE: return t->status_mask ? 1u : 0u;
    case IK_FIRE: return d->power < t->hp ? d->power : t->hp;
    case IK_RAIN: return b->weather ? 0u : 1u;
    case IK_CLEAR: return b->weather ? 1u : 0u;
    case IK_PROMOTE: return t->class_id == d->p1 && t->level >= d->p3;
    case IK_RECLASS: return t->class_id != d->p1 && class_defs[t->class_id].family != FAM_CIVILIAN;
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
    if (def->kind == IK_BOOK) {
        uint8_t r = strategy_book_check(b, user, def->p1, tx, ty);
        return r == SR_OK ? IR_OK : (r == SR_RANGE || r == SR_LOS ? IR_RANGE : (r == SR_WEATHER ? IR_NO_EFFECT : IR_NO_TARGET));
    }
    if (def->kind >= IK_WEAPON) return IR_NO_EFFECT;                  /* equipment / plot: not usable */
    unit_load(user, &c);
    if (tx >= grid_w || ty >= grid_h) return IR_RANGE;
    d = manhattan(c.x, c.y, tx, ty);
    if (d > def->range) return IR_RANGE;
    if (def->kind == IK_RAIN || def->kind == IK_CLEAR || def->kind == IK_PROMOTE || def->kind == IK_RECLASS) {
        if (item_value(b, it, &c)) return IR_OK;                       /* self only (range 0) */
        return (def->kind == IK_PROMOTE || def->kind == IK_RECLASS) ? IR_CLASS : IR_NO_EFFECT;
    }
    if (def->kind == IK_FIRE && d > 1 && !los_clear(c.x, c.y, tx, ty)) return IR_RANGE;
    ti = unit_at(b, tx, ty, &t);
    if (ti == 0xFF) return IR_NO_TARGET;
    if ((def->kind == IK_FIRE) != factions_hostile(c.faction, t.faction)) return IR_NO_TARGET;
    if (def->kind == IK_FIRE && ti == user) return IR_NO_TARGET;
    return item_value(b, it, &t) ? IR_OK : IR_NO_EFFECT;
}

static void mark_used(Battle *b, uint8_t user)
{
    ActiveUnit c;
    unit_load(user, &c);
    if (unit_alive(&c)) { c.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED); unit_store(user, &c); }
    battle_check_outcome(b, 0);
}

uint8_t item_use(Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty, ItemResult *r) CORE_API
{
    ActiveUnit t;
    uint8_t chk = item_check(b, user, slot, tx, ty), it, ti, k;
    const ItemDef *def;
    r->item = ITEM_NONE; r->target = 0xFF; r->killed = 0; r->amount = 0;
    if (chk != IR_OK) return chk;
    it = inv_get(user, slot);
    def = &item_defs[it];
    if (def->kind == IK_BOOK) return IR_NO_EFFECT;                    /* books: item_use_book */
    r->item = it;
    inv_set(user, slot, ITEM_NONE);                      /* consumed first: a reset never duplicates it */
    if (def->kind == IK_RAIN || def->kind == IK_CLEAR) {
        b->weather = def->kind == IK_RAIN ? 1u : 0u;
        r->target = user; r->amount = 1;
    } else if (def->kind == IK_PROMOTE || def->kind == IK_RECLASS) {
        unit_load(user, &t);                             /* 5.5: keep L/XP and the deficit */
        t.class_id = def->kind == IK_PROMOTE ? def->p2 : def->p1;
        unit_store(user, &t);
        item_rederive(user);
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
    mark_used(b, user);
    return IR_OK;
}

uint8_t item_use_book(Battle *b, uint8_t user, uint8_t slot, uint8_t cx, uint8_t cy, StrategyResult *sr) CORE_API
{
    uint8_t it = inv_get(user, slot), r;
    sr->n = 0; sr->xp = 0; sr->levels = 0;
    if (it == ITEM_NONE) return IR_EMPTY;
    if (item_defs[it].kind != IK_BOOK) return IR_NO_EFFECT;
    r = item_check(b, user, slot, cx, cy);
    if (r != IR_OK) return r;
    inv_set(user, slot, ITEM_NONE);                      /* consumed even if every target is missed */
    strategy_book_cast(b, user, item_defs[it].p1, cx, cy, sr);   /* marks the action, checks the outcome */
    return IR_OK;
}

/* ---------------------------------------------------------------- exchange (7.3) */
static void swap_slots(uint8_t a, uint8_t sa, uint8_t b, uint8_t sb)
{
    uint8_t ia = inv_get(a, sa), ib = inv_get(b, sb);
    inv_set(b, sb, ia);
    inv_set(a, sa, ib);
}

uint8_t item_exchange(uint8_t actor, uint8_t sa, uint8_t recv, uint8_t sb) CORE_API
{
    ActiveUnit a, r;
    unit_load(actor, &a);
    unit_load(recv, &r);
    if (actor == recv || !unit_alive(&a) || !unit_alive(&r) || factions_hostile(a.faction, r.faction)) return IR_NO_TARGET;
    if (manhattan(a.x, a.y, r.x, r.y) != 1) return IR_RANGE;
    if (sa >= INV_SLOTS || sb >= INV_SLOTS) return IR_EMPTY;
    if (inv_get(actor, sa) == ITEM_NONE && inv_get(recv, sb) == ITEM_NONE) return IR_EMPTY;
    if (inv_slot_equipped(actor, sa) || inv_slot_equipped(recv, sb)) return IR_EQUIPPED;
    swap_slots(actor, sa, recv, sb);
    a.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);
    unit_store(actor, &a);
    return IR_OK;
}

uint8_t item_exchange_camp(uint8_t a, uint8_t sa, uint8_t b, uint8_t sb) CORE_API
{
    uint8_t ea, eb;
    if (sa >= INV_SLOTS || sb >= INV_SLOTS || (a == b && sa == sb)) return IR_EMPTY;
    if (inv_get(a, sa) == ITEM_NONE && inv_get(b, sb) == ITEM_NONE) return IR_EMPTY;
    ea = inv_slot_equipped(a, sa); eb = inv_slot_equipped(b, sb);
    swap_slots(a, sa, b, sb);                            /* inv_set drops the equip marks of both slots */
    if (ea) item_rederive(a);
    if (eb) item_rederive(b);
    return IR_OK;
}

/* ---------------------------------------------------------------- treasures (7.4) */
uint8_t treasure_at(uint8_t x, uint8_t y) CORE_API
{
    TreasureDef d;
    uint8_t i;
    for (i = 0; treasure_def(i, &d); i++)
        if (d.x == x && d.y == y && !vm_flag_get(SCOPE_BATTLE, d.flag)) return i;
    return 0xFF;
}

uint8_t treasure_take(uint8_t i, uint8_t receiver) CORE_API
{
    TreasureDef d;
    if (!treasure_def(i, &d) || vm_flag_get(SCOPE_BATTLE, d.flag)) return IR_NO_TARGET;
    if (inv_count(receiver) >= INV_SLOTS) return IR_FULL;
    vm_flag_set(SCOPE_BATTLE, d.flag, 1);                /* taken first (7.4), then the item */
    inv_add(receiver, d.item);
    return IR_OK;
}

uint8_t inv_first_free(const Battle *b, uint8_t faction) CORE_API
{
    ActiveUnit u;
    uint8_t i;
    for (i = 0; i < b->n_units; i++) {
        unit_load(i, &u);
        if (unit_alive(&u) && u.faction == faction && inv_count(i) < INV_SLOTS) return i;
    }
    return 0xFF;
}

/* ---------------------------------------------------------------- prize queue (7.4) */
static uint8_t pq_n(void) { uint8_t n; sram_read(12, PRIZE_SRAM_OFF, &n, 1); return n <= PRIZE_MAX ? n : 0u; }
static void pq_set_n(uint8_t n) { sram_write(12, PRIZE_SRAM_OFF, &n, 1); }

uint8_t prize_count(void) CORE_API { return pq_n(); }

uint8_t prize_get(uint8_t i) CORE_API
{
    uint8_t v;
    if (i >= pq_n()) return ITEM_NONE;
    sram_read(12, (uint16_t)(PRIZE_SRAM_OFF + 1u + i), &v, 1);
    return v;
}

uint8_t prize_push(uint8_t item) CORE_API
{
    uint8_t n = pq_n();
    if (n >= PRIZE_MAX || !item_known(item)) return 0;
    sram_write(12, (uint16_t)(PRIZE_SRAM_OFF + 1u + n), &item, 1);
    pq_set_n((uint8_t)(n + 1u));
    return 1;
}

void prize_remove(uint8_t i) CORE_API
{
    uint8_t n = pq_n(), k, v;
    if (i >= n) return;
    for (k = i; k + 1u < n; k++) {
        sram_read(12, (uint16_t)(PRIZE_SRAM_OFF + 2u + k), &v, 1);
        sram_write(12, (uint16_t)(PRIZE_SRAM_OFF + 1u + k), &v, 1);
    }
    pq_set_n((uint8_t)(n - 1u));
}

void prize_clear(void) CORE_API { pq_set_n(0); }
