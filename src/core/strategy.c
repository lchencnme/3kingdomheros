#ifdef __SDCC
#pragma bank 8
#endif
#include "strategy.h"
#include "grid.h"
#include "path.h"
#include "rules.h"

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

static uint8_t is_water(uint8_t t) { return t == TER_6 || t == TER_7 || t == TER_9; }   /* shallow/deep/bridge */

static uint8_t wall_adjacent(uint8_t x, uint8_t y)
{
    if (x && (grid_terrain((uint8_t)(x - 1), y) & 0x0F) == TER_8) return 1;
    if (y && (grid_terrain(x, (uint8_t)(y - 1)) & 0x0F) == TER_8) return 1;
    if (x + 1u < grid_w && (grid_terrain((uint8_t)(x + 1), y) & 0x0F) == TER_8) return 1;
    if (y + 1u < grid_h && (grid_terrain(x, (uint8_t)(y + 1)) & 0x0F) == TER_8) return 1;
    return 0;
}

/* per-target terrain condition (design doc 5.3) */
static uint8_t cell_ok(const StrategyDef *s, uint8_t x, uint8_t y, uint8_t rain)
{
    uint8_t t = grid_terrain(x, y) & 0x0F;
    switch (s->cond) {
    case SC_FIRE: return !rain && t != TER_7;
    case SC_WATER: return is_water(t);
    case SC_ROCK: return t == TER_5 || wall_adjacent(x, y);
    case SC_FLOOD: return rain && is_water(t);
    default: return 1;
    }
}

static uint8_t offensive(uint8_t effect) { return effect <= FX_ROCK || effect == FX_STATUS; }

/* unit at (x,y) that this strategy may affect (hostile for offensive, allied otherwise), else 0xFF */
static uint8_t target_at(const Battle *b, const ActiveUnit *c, const StrategyDef *s, uint8_t x, uint8_t y)
{
    uint8_t i;
    ActiveUnit u;
    for (i = 0; i < b->n_units; i++) {
        unit_load(i, &u);
        if (!unit_alive(&u) || u.x != x || u.y != y) continue;
        if (offensive(s->effect) != factions_hostile(c->faction, u.faction)) return 0xFF;
        return i;
    }
    return 0xFF;
}

uint8_t strategy_known(const ActiveUnit *c, uint8_t sid) CORE_API
{
    return (class_defs[c->class_id].learn_mask >> sid) & 1u && c->level >= strategy_defs[sid].learn;
}

uint8_t strategy_check(const Battle *b, uint8_t caster, uint8_t sid, uint8_t cx, uint8_t cy) CORE_API
{
    ActiveUnit c;
    const StrategyDef *s = &strategy_defs[sid];
    uint8_t d;
    unit_load(caster, &c);
    if (!strategy_known(&c, sid)) return SR_NOT_LEARNED;
    if (c.status_mask & (1u << ST_SILENCE)) return SR_SILENCED;
    if (c.mp < s->mp) return SR_NO_MP;
    if (cx >= grid_w || cy >= grid_h) return SR_RANGE;
    d = manhattan(c.x, c.y, cx, cy);
    if (d > s->range) return SR_RANGE;
    if (d > 1 && !los_clear(c.x, c.y, cx, cy)) return SR_LOS;
    if ((s->cond == SC_FIRE && b->weather) || (s->cond == SC_FLOOD && !b->weather)) return SR_WEATHER;
    if (target_at(b, &c, s, cx, cy) == 0xFF || !cell_ok(s, cx, cy, b->weather)) return SR_NO_TARGET;
    return SR_OK;
}

static void apply_status(ActiveUnit *u, uint8_t st, uint8_t ttl)
{
    uint8_t m = (uint8_t)(1u << st);
    if (!(u->status_mask & m) || u->ttl[st] < ttl) u->ttl[st] = ttl;   /* max(old, new), no stacking */
    u->status_mask |= m;
    u->expiry_eligible_mask &= (uint8_t)~m;                            /* not ticked this phase (6.4) */
}

uint8_t strategy_cast(Battle *b, uint8_t caster, uint8_t sid, uint8_t cx, uint8_t cy, StrategyResult *r) CORE_API
{
    static const int8_t DX[5] = {0, 0, -1, 1, 0}, DY[5] = {0, -1, 0, 0, 1};   /* centre, N, W, E, S */
    const StrategyDef *s = &strategy_defs[sid];
    ActiveUnit c, u;
    uint8_t k, cells = s->radius ? 5u : 1u, order[STRAT_MAX_TARGETS], n = 0, i, j;
    uint16_t xp = 0;
    uint8_t chk = strategy_check(b, caster, sid, cx, cy);
    r->n = 0; r->xp = 0; r->levels = 0;
    if (chk != SR_OK) return chk;
    unit_load(caster, &c);
    /* collect valid targets, then process in instance order (design doc 4.4) */
    for (k = 0; k < cells; k++) {
        int16_t x = (int16_t)cx + DX[k], y = (int16_t)cy + DY[k];
        uint8_t t;
        if (x < 0 || y < 0 || x >= grid_w || y >= grid_h) continue;
        if (!cell_ok(s, (uint8_t)x, (uint8_t)y, b->weather)) continue;   /* skipped: no RNG */
        t = target_at(b, &c, s, (uint8_t)x, (uint8_t)y);
        if (t == 0xFF) continue;
        order[n++] = t;
    }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && order[j - 1] > order[j]; j--) { uint8_t tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp; }
    c.mp = (uint16_t)(c.mp - s->mp);
    for (i = 0; i < n; i++) {
        uint8_t t = order[i], hit = 1, killed = 0;
        uint16_t amount = 0;
        unit_load(t, &u);
        if (offensive(s->effect)) {
            uint8_t pct, var;
            uint16_t roll;
            UnitBase bc, bt;
            unit_base(caster, &bc);
            unit_base(t, &bt);
            pct = rules_mag_hit(bc.intl, bt.intl, c.morale);
            roll = rng_rand(&b->rng, 100);
            var = (uint8_t)(90 + rng_rand(&b->rng, 21));   /* always 2 draws, also for pure status */
            hit = roll < pct;
            if (hit && s->effect != FX_STATUS) {
                uint8_t tt = grid_terrain(u.x, u.y) & 0x0F;
                amount = rules_mag_damage(s->power, c.mag, u.res, c.morale,
                                          rules_element_percent(s->effect, tt, b->weather), var, 0);
                if (amount >= u.hp) {
                    amount = u.hp;
                    grid_occ_clear(u.x, u.y);
                    u.hp = 0; u.action_flags |= AF_RETREAT; u.status_mask = 0;
                    killed = 1;
                } else {
                    u.hp = (uint16_t)(u.hp - amount);
                    if (s->status != ST_NONE) apply_status(&u, s->status, s->ttl);   /* fire -> BURN */
                }
                xp += rules_xp_hit(c.level, u.level, 1, killed);
            } else if (hit) {
                apply_status(&u, s->status, s->ttl);
                xp += 5;
            } else {
                xp += 1;
            }
        } else if (s->effect == FX_HEAL) {
            uint16_t want = (uint16_t)(s->power + c.mag / 2u), gap = (uint16_t)(u.max_hp - u.hp);
            amount = want < gap ? want : gap;
            u.hp = (uint16_t)(u.hp + amount);
            hit = amount > 0;
            xp += rules_xp_heal(amount, u.max_hp);
        } else if (s->effect == FX_MORALE) {
            uint16_t m = (uint16_t)u.morale + s->power;
            amount = (uint16_t)((m > 100u ? 100u : m) - u.morale);
            u.morale = (uint8_t)(m > 100u ? 100u : m);
            hit = amount > 0;
        } else {                                       /* cleanse: all five negative states */
            hit = u.status_mask != 0;
            u.status_mask = 0; u.expiry_eligible_mask = 0;
            for (k = 0; k < 5; k++) u.ttl[k] = 0;
            if (hit) xp += 5;
        }
        if (t == caster) { u.mp = c.mp; c = u; }       /* self-target (heal/morale/cleanse) */
        unit_store(t, &u);
        r->unit[r->n] = t; r->hit[r->n] = hit; r->amount[r->n] = amount; r->killed[r->n] = killed; r->n++;
    }
    if (xp > 60u) xp = 60u;                            /* one group command: at most 60 XP (5.4) */
    r->xp = (uint8_t)xp;
    unit_load(caster, &u);
    if (unit_alive(&u)) {
        uint16_t old_mhp = u.max_hp, old_mmp = u.max_mp;
        u.mp = c.mp;
        u.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);
        r->levels = rules_add_xp(&u.level, &u.exp, r->xp);
        unit_store(caster, &u);
        if (r->levels) {
            /* re-derive through battle_spawn-like path: keep the deficit (5.4) */
            StatIn si; Derived d; UnitBase base;
            unit_base(caster, &base);
            si.cls = u.class_id; si.level = u.level; si.str = base.str; si.intl = base.intl; si.cmd = base.cmd;
            si.mod_hp = si.mod_mp = si.mod_atk = si.mod_def = si.mod_mag = si.mod_res = 0; si.mod_agi = 0; si.mod_move = 0;
            rules_derive(&si, &d);
            u.hp = rules_keep_deficit(old_mhp, u.hp, d.max_hp, 1);
            u.mp = rules_keep_deficit(old_mmp, u.mp, d.max_mp, 0);
            u.max_hp = d.max_hp; u.max_mp = d.max_mp; u.atk = d.atk; u.def = d.def;
            u.mag = d.mag; u.res = d.res; u.agility = d.agi; u.move = d.move;
            unit_store(caster, &u);
        }
    }
    battle_check_outcome(b, 0);
    return SR_OK;
}
