#include "ai.h"
#include "grid.h"
#include "types.h"

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

void ai_invalidate(AiScratch *s) { s->field_valid = 0; }

static void build_field(Battle *b, AiScratch *s, uint8_t mtype, uint8_t side)
{
    uint16_t *cells = s->cells;
    uint8_t i, n = 0, k;
    FieldJob j;
    ActiveUnit u;
    for (i = 0; i < b->n_units; i++) {
        uint16_t c;
        unit_load(i, &u);
        if (!unit_alive(&u) || faction_side(u.faction) == side) continue;
        c = (uint16_t)u.y * grid_w + u.x;
        /* insertion sort, unique ascending */
        for (k = n; k > 0 && cells[k - 1] > c; k--) cells[k] = cells[k - 1];
        cells[k] = c;
        n++;
    }
    field_begin(&j, mtype, side, cells, n);
    while (!field_step(&j, 512)) {}
    s->field_mtype = mtype; s->field_side = side; s->field_valid = 1;
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

void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out)
{
    ActiveUnit a, t;
    uint8_t i, side, mtype, ly, lx, have = 0;
    uint16_t here_field, bcnt = 0, bcell = 0;
    uint8_t bcost = 0;
    unit_load(actor, &a);
    side = faction_side(a.faction);
    mtype = class_defs[a.class_id].move_type;

    out->x = a.x; out->y = a.y; out->action = ACT_WAIT; out->target = 0xFF; out->score = 0;
    if (!battle_can_act(&a)) return;

    path_move_range(mr, a.x, a.y, (a.status_mask & (1u << ST_ROOT)) ? 0 : a.move, mtype, side);

    /* opponent list: alive hostiles, nearest first (deterministic: distance, then id) */
    s->n_opp = 0;
    for (i = 0; i < b->n_units; i++) {
        uint8_t k, d;
        AiOpp *o;
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
        o->id = i; o->x = t.x; o->y = t.y; o->cls = t.class_id; o->move = t.move; o->hp = t.hp;
        o->rmax = class_defs[t.class_id].range_max;
        o->terrain = grid_terrain(t.x, t.y) & 0x0F;
        { uint8_t q; for (q = 0; q < 10; q++) o->threat[q] = 0xFFFF; }
    }

    if (!s->field_valid || s->field_mtype != mtype || s->field_side != side) build_field(b, s, mtype, side);
    here_field = fld_get((uint16_t)a.y * grid_w + a.x);

    for (ly = 0; ly < MR_DIM; ly++) {
        for (lx = 0; lx < MR_DIM; lx++) {
            int16_t gx = mr->bx + lx, gy = mr->by + ly;
            uint8_t ex, ey, cost, et, oi;
            uint16_t ef, cell;
            int32_t base;
            uint32_t danger = 0;
            if (gx < 0 || gy < 0 || gx >= grid_w || gy >= grid_h) continue;
            ex = (uint8_t)gx; ey = (uint8_t)gy;
            if (!path_endpoint_ok(mr, ex, ey)) continue;
            if (mode == AI_HOLD && !(ex == a.x && ey == a.y)) continue;
            cost = path_dist(mr, ex, ey);
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
            /* ATTACK candidates */
            for (oi = 0; oi < s->n_opp; oi++) {
                AiOpp *o = &s->opp[oi];
                AttackPreview p;
                int32_t sc;
                uint32_t e, ce = 0;
                if (class_defs[a.class_id].range_max == 0) break;
                unit_load(o->id, &t);
                if (!battle_in_range(&a, ex, ey, &t)) continue;
                battle_preview(&a, ex, ey, &t, b->weather, &p);
                e = ((uint32_t)p.dmg * p.hit_pct) / 100u;
                if (e > t.hp) e = t.hp;
                sc = base + W_DMG * (int32_t)(e > 9999u ? 9999u : e);
                if (p.dmg >= t.hp && p.hit_pct >= 50) sc += W_KILL;
                if (p.counter) {
                    ce = ((uint32_t)p.c_dmg * p.c_hit_pct) / 100u;
                    if (ce > a.hp) ce = a.hp;
                    sc -= W_COUNTER * (int32_t)ce;
                }
                if (better(sc, (uint16_t)ce, cost, o->id, cell, ACT_ATTACK, out, bcnt, bcost, bcell)) {
                    out->x = ex; out->y = ey; out->action = ACT_ATTACK; out->target = o->id; out->score = sc;
                    bcnt = (uint16_t)ce; bcost = cost; bcell = cell;
                }
            }
        }
    }
}
