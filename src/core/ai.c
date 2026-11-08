#ifdef __SDCC
#pragma bank 4
#endif
#include "ai.h"
#include "grid.h"
#include "types.h"

#ifdef __SDCC
#include <gb/gb.h>
extern volatile uint16_t dbg_ai_t_range, dbg_ai_t_field, dbg_ai_t_cand;
#define PROF_T() (sys_time)
#define PROF_SET(v, x) ((v) = (x))
#else
#define PROF_T() 0u
#define PROF_SET(v, x) ((void)(x))
#endif

#define W_KILL 1000000l
#define W_DMG 100l
#define W_COUNTER 80l
#define W_DANGER 40l
#define W_APPROACH 20l
#define W_MOVECOST 3l

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

void ai_invalidate(AiScratch *s) CORE_API { s->field_state = 0; }

void ai_field_request(Battle *b, AiScratch *s, uint8_t mtype, uint8_t side) CORE_API
{
    uint16_t *cells = s->cells;
    uint8_t i, n = 0, k;
    ActiveUnit u;
    for (i = 0; i < b->n_units; i++) {
        uint16_t c;
        unit_load(i, &u);
        if (!unit_alive(&u) || faction_side(u.faction) == side) continue;
        c = (uint16_t)u.y * grid_w + u.x;
        for (k = n; k > 0 && cells[k - 1] > c; k--) cells[k] = cells[k - 1];   /* unique ascending */
        cells[k] = c;
        n++;
    }
    s->field_mtype = mtype; s->field_side = side; s->field_state = 1;
    field_begin(&s->job, mtype, side, cells, n);
    if (s->job.done) s->field_state = 2;
}

uint8_t ai_field_pump(AiScratch *s, uint16_t budget) CORE_API
{
    if (s->field_state == 1 && field_step(&s->job, budget)) s->field_state = 2;
    return s->field_state == 2;
}

/* expected damage of opp o attacking actor a standing on terrain t (cached) */
static uint16_t threat(AiScratch *s, uint8_t oi, const ActiveUnit *a, uint8_t t, uint8_t weather)
{
    AiOpp *o = &s->opp[oi];
    if (o->threat[t] == 0xFFFF) {
        ActiveUnit ou;
        PhysIn in;
        uint8_t pct;
        uint32_t e;
        unit_load(o->id, &ou);
        in.atk = ou.atk; in.def = a->def; in.hp = ou.hp; in.max_hp = ou.max_hp;
        in.fam_a = class_defs[ou.class_id].family; in.fam_d = class_defs[a->class_id].family;
        in.morale = ou.morale; in.terrain_d = t; in.rain = weather; in.variance = 100;
        pct = rules_phys_hit(ou.agility, a->agility, ou.morale, t);
        e = ((uint32_t)rules_phys_damage(&in, 0) * pct) / 100u;
        o->threat[t] = (uint16_t)(e > 9999u ? 9999u : e);
    }
    return o->threat[t];
}

/* returns 1 if candidate (sc, cnt, cost, tgt, cell, act) beats best */
static uint8_t better(int32_t sc, uint16_t cnt, uint8_t cost, uint8_t tgt, uint16_t cell, uint8_t act,
                      const AiCommand *best, uint16_t bcnt, uint8_t bcost, uint16_t bcell)
{
    if (sc != best->score) return sc > best->score;
    if (cnt != bcnt) return cnt < bcnt;
    if (cost != bcost) return cost < bcost;
    if (tgt != best->target) return tgt < best->target;
    if (cell != bcell) return cell < bcell;
    return act < best->action;
}

static uint16_t attack_value(AiScratch *s, uint8_t oi, const ActiveUnit *a, uint8_t weather)
{
    AiOpp *o = &s->opp[oi];
    if (o->e_att == 0xFFFF) {
        ActiveUnit t;
        AttackPreview p;
        uint32_t e;
        unit_load(o->id, &t);
        battle_preview(a, a->x, a->y, &t, weather, &p);   /* endpoint only matters for range/LOS/counter */
        e = ((uint32_t)p.dmg * p.hit_pct) / 100u;
        if (e > t.hp) e = t.hp;
        o->e_att = (uint16_t)e;
        o->kill = p.dmg >= t.hp && p.hit_pct >= 50;
    }
    return o->e_att;
}

void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out) CORE_API
{
    ActiveUnit a, t;
    uint8_t i, side, mtype, ly, lx, have = 0, armin, armax;
    uint16_t here_field, bcnt = 0, bcell = 0;
    uint8_t bcost = 0;
    uint16_t pt0;
    unit_load(actor, &a);
    side = faction_side(a.faction);
    mtype = class_defs[a.class_id].move_type;
    armin = class_defs[a.class_id].range_min;
    armax = class_defs[a.class_id].range_max;

    out->x = a.x; out->y = a.y; out->action = ACT_WAIT; out->target = 0xFF; out->score = 0;
    if (!battle_can_act(&a)) return;

    pt0 = PROF_T();
    path_move_range(mr, a.x, a.y, (a.status_mask & (1u << ST_ROOT)) ? 0 : a.move, mtype, side);
    PROF_SET(dbg_ai_t_range, PROF_T() - pt0); pt0 = PROF_T();

    /* opponent list: alive hostiles, nearest first (deterministic: distance, then id) */
    s->n_opp = 0;
    for (i = 0; i < b->n_units; i++) {
        uint8_t k, d, q;
        AiOpp *o;
        const ClassDef *c;
        unit_load(i, &t);
        if (!unit_alive(&t) || !factions_hostile(a.faction, t.faction)) continue;
        d = manhattan(a.x, a.y, t.x, t.y);
        k = s->n_opp;
        if (k == AI_MAX_OPP) {
            if (d >= manhattan(a.x, a.y, s->opp[k - 1].x, s->opp[k - 1].y)) continue;
            k--;
        } else s->n_opp++;
        while (k > 0 && manhattan(a.x, a.y, s->opp[k - 1].x, s->opp[k - 1].y) > d) { s->opp[k] = s->opp[k - 1]; k--; }
        o = &s->opp[k];
        c = &class_defs[t.class_id];
        o->id = i; o->x = t.x; o->y = t.y; o->move = t.move; o->hp = t.hp;
        o->rmin = c->range_min; o->rmax = c->range_max;
        o->can_counter = c->counter && !(t.action_flags & AF_COUNTER) && !(t.status_mask & (1u << ST_CONFUSE));
        o->e_att = 0xFFFF; o->kill = 0;
        for (q = 0; q < 10; q++) o->threat[q] = 0xFFFF;
    }

    if (s->field_state == 0 || s->field_mtype != mtype || s->field_side != side) ai_field_request(b, s, mtype, side);
    while (!ai_field_pump(s, 1024)) {}             /* finish synchronously if the phase-start job is not done */
    PROF_SET(dbg_ai_t_field, PROF_T() - pt0); pt0 = PROF_T();
    here_field = fld_get((uint16_t)a.y * grid_w + a.x);

    for (ly = 0; ly < MR_DIM; ly++) {
        int16_t gy = mr->by + ly;
        if (gy < 0 || gy >= grid_h) continue;
        for (lx = 0; lx < MR_DIM; lx++) {
            int16_t gx = mr->bx + lx;
            uint8_t ex, ey, cost, et, oi;
            uint16_t ef, cell;
            int32_t base;
            uint32_t danger = 0;
            if (gx < 0 || gx >= grid_w) continue;
            cost = mr->dist[(uint16_t)ly * MR_DIM + lx];
            if (cost == MR_INF) continue;
            ex = (uint8_t)gx; ey = (uint8_t)gy;
            if (!(ex == a.x && ey == a.y)) {
                if (mode == AI_HOLD) continue;
                if (grid_occ(ex, ey)) continue;              /* allies may be crossed, not ended on */
            }
            cell = (uint16_t)ey * grid_w + ex;
            et = grid_terrain(ex, ey) & 0x0F;

            base = -W_MOVECOST * cost;
            ef = fld_get(cell);
            if (here_field != FIELD_INF && ef != FIELD_INF) {
                int32_t gain = (int32_t)here_field - (int32_t)ef;
                base += W_APPROACH * clamp32(gain, -255, 255);
            }
            for (oi = 0; oi < s->n_opp; oi++) {
                AiOpp *o = &s->opp[oi];
                if (manhattan(o->x, o->y, ex, ey) <= (uint8_t)(o->move + o->rmax))
                    danger += threat(s, oi, &a, et, b->weather);
            }
            /* AGGRESSIVE only fears lethal cells; cautious modes weigh all danger (design doc 8.3) */
            if (mode == AI_AGGRESSIVE) {
                if (danger >= a.hp) base -= W_DANGER * (int32_t)a.hp;
            } else {
                if (danger > a.hp) danger = a.hp;
                base -= W_DANGER * (int32_t)danger;
            }

            /* WAIT candidate */
            if (!have || better(base, 0, cost, 0xFF, cell, ACT_WAIT, out, bcnt, bcost, bcell)) {
                out->x = ex; out->y = ey; out->action = ACT_WAIT; out->target = 0xFF; out->score = base;
                bcnt = 0; bcost = cost; bcell = cell; have = 1;
            }
            /* ATTACK candidates: cheap range filter first, LOS only for ranged shots */
            if (armax == 0) continue;
            for (oi = 0; oi < s->n_opp; oi++) {
                AiOpp *o = &s->opp[oi];
                uint8_t d = manhattan(ex, ey, o->x, o->y);
                int32_t sc;
                uint16_t e, ce = 0;
                if (d < armin || d > armax) continue;
                if (d > 1 && !los_clear(ex, ey, o->x, o->y)) continue;
                e = attack_value(s, oi, &a, b->weather);
                sc = base + W_DMG * (int32_t)e;
                if (o->kill) sc += W_KILL;
                else if (o->can_counter && d >= o->rmin && d <= o->rmax) {
                    ce = threat(s, oi, &a, et, b->weather);
                    if (ce > a.hp) ce = a.hp;
                    sc -= W_COUNTER * (int32_t)ce;
                }
                if (better(sc, ce, cost, o->id, cell, ACT_ATTACK, out, bcnt, bcost, bcell)) {
                    out->x = ex; out->y = ey; out->action = ACT_ATTACK; out->target = o->id; out->score = sc;
                    bcnt = ce; bcost = cost; bcell = cell;
                }
            }
        }
    }
    PROF_SET(dbg_ai_t_cand, PROF_T() - pt0);
}
