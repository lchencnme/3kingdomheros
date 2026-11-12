#ifdef __SDCC
#pragma bank 12
#endif
#include "ai.h"
#include "grid.h"
#include "types.h"
#include "strategy.h"
#include "item.h"
#include "save.h"

typedef char ai_strat_fits[(sizeof(AiStrat) * AI_MAX_STRAT <= 256u) ? 1 : -1];   /* GB: D500..D5FF */
#ifndef __SDCC
AiStrat ai_strat_host[AI_MAX_STRAT];
ActiveUnit ai_tmp_host;
#endif

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
#define W_CTRL 3000l
#define W_HEAL 80l
#define W_PRICE 10l
#define W_MORALE 30l
#define W_MISSION 100000000l

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

void ai_invalidate(AiScratch *s) CORE_API { s->field_state = 0; }

/* ---------------------------------------------------------------- groups / overrides (8.2) */
static void rec_write(uint16_t off, const AiMode *m)
{
    uint8_t r[5];
    r[0] = m->mode; r[1] = (uint8_t)(m->target & 0xFF); r[2] = (uint8_t)(m->target >> 8); r[3] = m->leash; r[4] = m->flags;
    sram_write(AI_SRAM_BANK, off, r, 5);
}

static void rec_read(uint16_t off, AiMode *m)
{
    uint8_t r[5];
    sram_read(AI_SRAM_BANK, off, r, 5);
    m->mode = r[0]; m->target = (uint16_t)(r[1] | ((uint16_t)r[2] << 8)); m->leash = r[3]; m->flags = r[4];
}

void ai_groups_init(const uint8_t *recs, uint8_t n) CORE_API
{
    AiMode d;
    uint8_t i;
    d.mode = AI_HOLD; d.target = AI_NO_TARGET; d.leash = 3; d.flags = 0;
    for (i = 0; i < MAX_UNITS; i++) rec_write((uint16_t)(AI_SRAM_GROUPS + (uint16_t)i * 5u), &d);
    d.mode = 0; d.target = 0; d.leash = 0;
    for (i = 0; i < MAX_UNITS; i++) rec_write((uint16_t)(AI_SRAM_INST + (uint16_t)i * 5u), &d);    /* no overrides */
    for (i = 0; i < n; i++) {
        const uint8_t *r = recs + (uint16_t)i * 5u;
        if (r[0] >= MAX_UNITS) continue;
        d.mode = r[1]; d.target = (uint16_t)(r[2] | ((uint16_t)r[3] << 8)); d.leash = r[4]; d.flags = 0;
        rec_write((uint16_t)(AI_SRAM_GROUPS + (uint16_t)r[0] * 5u), &d);
    }
}

void ai_set(uint16_t subject, uint8_t mode, uint16_t target, uint8_t leash) CORE_API
{
    AiMode m;
    m.mode = mode; m.target = target; m.leash = leash;
    if (subject & 0x8000u) {
        if ((subject & 0x7Fu) >= MAX_UNITS) return;
        m.flags = 0;                                     /* groups keep their instance overrides */
        rec_write((uint16_t)(AI_SRAM_GROUPS + (subject & 0x7Fu) * 5u), &m);
    } else if (subject < MAX_UNITS) {
        m.flags = AIF_OVERRIDE;                          /* SET_AI resets the alert bit */
        rec_write((uint16_t)(AI_SRAM_INST + subject * 5u), &m);
    }
}

void ai_resolve(uint8_t unit, AiMode *m) CORE_API
{
    AiMode inst;
    ActiveUnit u;
    rec_read((uint16_t)(AI_SRAM_INST + (uint16_t)unit * 5u), &inst);
    if (inst.flags & AIF_OVERRIDE) { *m = inst; return; }
    unit_load(unit, &u);
    rec_read((uint16_t)(AI_SRAM_GROUPS + (uint16_t)(u.ai_id < MAX_UNITS ? u.ai_id : 0) * 5u), m);
    m->flags = inst.flags & AIF_ALERT;
}

static void set_alert(uint8_t unit)
{
    AiMode inst;
    rec_read((uint16_t)(AI_SRAM_INST + (uint16_t)unit * 5u), &inst);
    inst.flags |= AIF_ALERT;
    rec_write((uint16_t)(AI_SRAM_INST + (uint16_t)unit * 5u), &inst);
}

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
    s->field_mtype = mtype; s->field_side = side; s->field_state = 1; s->field_src = AI_NO_TARGET;
    field_begin(&s->job, mtype, side, cells, n);
    if (s->job.done) s->field_state = 2;
}

/* single-source field (GUARD/ESCAPE target cell, or a designated target unit's cell) */
static void field_request_cell(AiScratch *s, uint8_t mtype, uint8_t side, uint16_t cell)
{
    s->cells[0] = cell;
    s->field_mtype = mtype; s->field_side = side; s->field_state = 1; s->field_src = cell;
    field_begin(&s->job, mtype, side, s->cells, 1);
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

#define SB AI_STRAT_BUF

/* strategy candidates (8.3): every castable offensive strategy on every reachable opponent, valued
 * once per decision; plus the best self-use hp item. Own frame: ai_decide sits on the deepest chain. */
static void prepare_special(Battle *b, AiScratch *s, const ActiveUnit *ap, uint8_t actor)
{
#define a (*ap)
    uint8_t i;
    s->n_strat = 0;
    if (class_defs[a.class_id].learn_mask && a.mp && !(a.status_mask & (1u << ST_SILENCE))
        && class_defs[a.class_id].family != FAM_CIVILIAN) {
        uint8_t sid;
        for (sid = 0; sid < NUM_STRATEGIES; sid++) {
            const StrategyDef *sd = &strategy_defs[sid];
            if (!strategy_known(&a, sid) || a.mp < sd->mp) continue;
            if (sd->effect == FX_HEAL || sd->effect == FX_MORALE) {
                /* support strategies on damaged / shaken allies (incl. self: centre = the endpoint) */
                for (i = 0; i < b->n_units && s->n_strat < AI_MAX_STRAT; i++) {
                    StratEstimate se;
                    uint8_t ux, uy;
                    unit_load(i, AI_TMP_UNIT);
                    if (!unit_alive(AI_TMP_UNIT) || factions_hostile(a.faction, AI_TMP_UNIT->faction)) continue;
                    ux = AI_TMP_UNIT->x; uy = AI_TMP_UNIT->y;
                    if (manhattan(a.x, a.y, ux, uy) > (uint8_t)(a.move + sd->range)) continue;
                    if (!strategy_estimate(b, &a, actor, sid, ux, uy, &se, AI_TMP_UNIT) || (!se.heal && !se.morale)) continue;
                    SB[s->n_strat].sid = sid; SB[s->n_strat].range = sd->range;
                    SB[s->n_strat].cx = i == actor ? 0xFFu : ux; SB[s->n_strat].cy = uy;
                    SB[s->n_strat].val = W_HEAL * (int32_t)se.heal + W_MORALE * (int32_t)se.morale;
                    s->n_strat++;
                }
                continue;
            }
            for (i = 0; i < s->n_opp && s->n_strat < AI_MAX_STRAT; i++) {
                AiOpp *o = &s->opp[i];
                StratEstimate se;
                if (manhattan(a.x, a.y, o->x, o->y) > (uint8_t)(a.move + sd->range)) continue;
                if (!strategy_estimate(b, &a, actor, sid, o->x, o->y, &se, AI_TMP_UNIT) || (!se.dmg && !se.ctrl)) continue;
                SB[s->n_strat].sid = sid; SB[s->n_strat].cx = o->x; SB[s->n_strat].cy = o->y; SB[s->n_strat].range = sd->range;
                SB[s->n_strat].val = W_DMG * (int32_t)(se.dmg > 9999u ? 9999u : se.dmg) + W_KILL * (int32_t)se.kills
                                     + W_CTRL * (int32_t)se.ctrl;
                s->n_strat++;
            }
        }
    }
    /* fire pot (7.2): fixed 120, range 2, no counter; scored like a strategy minus 10 x price */
    for (i = 0; i < INV_SLOTS; i++) {
        uint8_t it = inv_get(actor, i), k, rng;
        uint16_t pw, pr;
        if (it == ITEM_NONE || item_kind(it) != IK_FIRE) continue;
        pw = item_field(it, 0); pr = item_field(it, 1); rng = (uint8_t)item_field(it, 2);
        for (k = 0; k < s->n_opp && s->n_strat < AI_MAX_STRAT; k++) {
            AiOpp *o = &s->opp[k];
            uint16_t dmg = pw < o->hp ? pw : o->hp;
            if (manhattan(a.x, a.y, o->x, o->y) > (uint8_t)(a.move + rng)) continue;
            SB[s->n_strat].sid = (uint8_t)(0x80u | i); SB[s->n_strat].range = rng;
            SB[s->n_strat].cx = o->x; SB[s->n_strat].cy = o->y;
            SB[s->n_strat].val = W_DMG * (int32_t)dmg + (pw >= o->hp ? W_KILL : 0)
                                 - W_PRICE * (int32_t)pr;
            s->n_strat++;
        }
        break;                                           /* one pot is enough to value */
    }
    /* sort by value, best first (stable insertion sort; n <= 32): the endpoint loop can then stop at the
     * first candidate in range + LOS, so LOS is computed about once per endpoint */
    for (i = 1; i < s->n_strat; i++) {
        uint8_t j = i;
        AiStrat tmp = SB[i];
        while (j > 0 && SB[j - 1].val < tmp.val) { SB[j] = SB[j - 1]; j--; }
        SB[j] = tmp;
    }
    /* item candidate: best self-use hp item (heal value x 80 - price x 10); applies at any endpoint */
    s->item_slot = 0xFF; s->item_sc = 0;
    for (i = 0; i < INV_SLOTS; i++) {
        uint8_t it = inv_get(actor, i);
        uint16_t v;
        int32_t sc;
        if (it == ITEM_NONE || item_kind(it) != IK_HP) continue;
        v = item_value(b, it, &a);
        if (!v) continue;
        sc = W_HEAL * (int32_t)v - W_PRICE * (int32_t)item_field(it, 1);
        if (s->item_slot == 0xFF || sc > s->item_sc) { s->item_slot = i; s->item_sc = sc; }
    }

#undef a
}

static ActiveUnit ai_t;   /* static (fixed RAM) to keep the deepest call chain off the 512B stack */


/* HOLD alert (8.4, rule): can any opponent reach a cell from which it attacks a's cell? Exact local SPFA
 * per opponent passing the coarse distance test; uses mr as scratch (the caller recomputes a's range). */
static uint8_t hold_threatened(Battle *b, MoveRange *mr, const ActiveUnit *a)
{
    uint8_t i, ly, lx;
    for (i = 0; i < b->n_units; i++) {
        const ClassDef *c;
        uint8_t mv;
        unit_load(i, &ai_t);
        if (!unit_alive(&ai_t) || !factions_hostile(a->faction, ai_t.faction)) continue;
        c = &class_defs[ai_t.class_id];
        if (c->range_max == 0) continue;
        mv = (ai_t.status_mask & (1u << ST_ROOT)) ? 0 : ai_t.move;
        if (manhattan(ai_t.x, ai_t.y, a->x, a->y) > (uint8_t)(mv + c->range_max)) continue;
        path_move_range(mr, ai_t.x, ai_t.y, mv, c->move_type, faction_side(ai_t.faction));
        for (ly = 0; ly < MR_DIM; ly++) {
            int16_t gy = mr->by + ly;
            if (gy < 0 || gy >= grid_h) continue;
            for (lx = 0; lx < MR_DIM; lx++) {
                int16_t gx = mr->bx + lx;
                uint8_t d;
                if (gx < 0 || gx >= grid_w || mr->dist[(uint16_t)ly * MR_DIM + lx] == MR_INF) continue;
                if (!path_endpoint_ok(mr, (uint8_t)gx, (uint8_t)gy)) continue;
                d = manhattan((uint8_t)gx, (uint8_t)gy, a->x, a->y);
                if (d < c->range_min || d > c->range_max) continue;
                if (d > 1 && !los_clear((uint8_t)gx, (uint8_t)gy, a->x, a->y)) continue;
                return 1;
            }
        }
    }
    return 0;
}

/* the approach field a unit's decision will use (8.4): all opponents, or one source cell */
static uint16_t field_src_for(Battle *b, const ActiveUnit *a, const AiMode *md)
{
    uint8_t mode = md->mode;
    if (mode == AI_HOLD && (md->flags & AIF_ALERT)) mode = AI_AGGRESSIVE;
    if (mode == AI_ESCAPE && md->target < (uint16_t)grid_w * grid_h) return md->target;
    if (mode == AI_AGGRESSIVE && md->target < b->n_units) {
        ActiveUnit t_;
        unit_load((uint8_t)md->target, &t_);
        if (unit_alive(&t_)) return (uint16_t)t_.y * grid_w + t_.x;
    }
    (void)a;
    return AI_NO_TARGET;
}

void ai_field_prepare(Battle *b, AiScratch *s, uint8_t unit) CORE_API
{
    AiMode m;
    ActiveUnit u;
    uint8_t mtype, side;
    uint16_t src;
    ai_resolve(unit, &m);
    unit_load(unit, &u);
    mtype = class_defs[u.class_id].move_type; side = faction_side(u.faction);
    src = field_src_for(b, &u, &m);
    if (s->field_state != 0 && s->field_mtype == mtype && s->field_side == side && s->field_src == src) return;
    if (src == AI_NO_TARGET) ai_field_request(b, s, mtype, side);
    else field_request_cell(s, mtype, side, src);
}

void ai_danger_add(Battle *b, MoveRange *mr, uint8_t unit, uint8_t *bits) CORE_API
{
    const ClassDef *c;
    uint8_t ly, lx, mv;
    (void)b;
    unit_load(unit, &ai_t);
    if (!unit_alive(&ai_t)) return;
    c = &class_defs[ai_t.class_id];
    if (c->range_max == 0) return;
    mv = (ai_t.status_mask & (1u << ST_ROOT)) ? 0 : ai_t.move;
    path_move_range(mr, ai_t.x, ai_t.y, mv, c->move_type, faction_side(ai_t.faction));
    for (ly = 0; ly < MR_DIM; ly++) {
        int16_t gy = mr->by + ly;
        if (gy < 0 || gy >= grid_h) continue;
        for (lx = 0; lx < MR_DIM; lx++) {
            int16_t gx = mr->bx + lx, dy;
            if (gx < 0 || gx >= grid_w || mr->dist[(uint16_t)ly * MR_DIM + lx] == MR_INF) continue;
            if (!path_endpoint_ok(mr, (uint8_t)gx, (uint8_t)gy)) continue;
            for (dy = -(int16_t)c->range_max; dy <= (int16_t)c->range_max; dy++) {
                int16_t ty = gy + dy, span = (int16_t)c->range_max - (dy < 0 ? -dy : dy), dx;
                if (ty < 0 || ty >= grid_h) continue;
                for (dx = -span; dx <= span; dx++) {
                    int16_t tx = gx + dx;
                    uint8_t d = (uint8_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
                    uint16_t cell;
                    if (tx < 0 || tx >= grid_w || d < c->range_min) continue;
                    cell = (uint16_t)ty * grid_w + (uint16_t)tx;
                    if (bits[cell >> 3] & (1u << (cell & 7u))) continue;
                    if (d > 1 && !los_clear((uint8_t)gx, (uint8_t)gy, (uint8_t)tx, (uint8_t)ty)) continue;
                    bits[cell >> 3] |= (uint8_t)(1u << (cell & 7u));
                }
            }
        }
    }
}

uint8_t ai_hold_update(Battle *b, MoveRange *mr, uint8_t unit) CORE_API
{
    AiMode m;
    ActiveUnit u;
    ai_resolve(unit, &m);
    if (m.mode != AI_HOLD || (m.flags & AIF_ALERT)) return (uint8_t)((m.flags & AIF_ALERT) != 0);
    unit_load(unit, &u);
    if (!unit_alive(&u) || !hold_threatened(b, mr, &u)) return 0;
    set_alert(unit);
    return 1;
}

void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out) CORE_API
{
    AiMode m;
    m.mode = mode; m.target = AI_NO_TARGET; m.leash = 3;
    m.flags = mode == AI_HOLD ? AIF_ALERT : 0;     /* legacy entry: plain HOLD never leaves its cell */
    if (mode == AI_HOLD) m.mode = AI_HOLD | 0x40u;
    ai_decide_mode(b, s, mr, actor, &m, out);
}

void ai_decide_mode(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, const AiMode *md, AiCommand *out) CORE_API
{
    ActiveUnit a;
#define t ai_t
    uint8_t i, side, mtype, ly, lx, have = 0, armin, armax, mode, leash, gpx = 0, gpy = 0;
    uint16_t here_field, bcnt = 0, bcell = 0, src = AI_NO_TARGET;
    uint8_t bcost = 0;
    uint16_t pt0;
    unit_load(actor, &a);
    mode = md->mode; leash = md->leash;
    side = faction_side(a.faction);
    mtype = class_defs[a.class_id].move_type;
    armin = class_defs[a.class_id].range_min;
    armax = class_defs[a.class_id].range_max;

    out->x = a.x; out->y = a.y; out->action = ACT_WAIT; out->target = 0xFF; out->score = 0;
    out->param = 0xFF; out->tx = a.x; out->ty = a.y;
    if (!battle_can_act(&a)) return;

    /* 8.2 modes. 0x40 marks the legacy never-alerting HOLD of ai_decide(). */
    if (mode == (AI_HOLD | 0x40u)) mode = AI_HOLD;
    else if (mode == AI_HOLD && (md->flags & AIF_ALERT)) mode = AI_AGGRESSIVE;   /* alert: ai_hold_update */
    if ((mode == AI_GUARD_POINT || mode == AI_ESCAPE) && md->target < (uint16_t)grid_w * grid_h) {
        src = md->target;
        gpy = (uint8_t)(md->target / grid_w); gpx = (uint8_t)(md->target - (uint16_t)gpy * grid_w);   /* once */
        if (mode == AI_GUARD_POINT) src = AI_NO_TARGET;   /* guards intercept: approach opponents, leash-limited */
    } else if ((mode == AI_ESCORT || mode == AI_AGGRESSIVE) && md->target < b->n_units) {
        unit_load((uint8_t)md->target, &t);
        if (unit_alive(&t)) {
            gpx = t.x; gpy = t.y;
            if (mode == AI_AGGRESSIVE) src = (uint16_t)t.y * grid_w + t.x;    /* designated target */
        } else if (mode == AI_ESCORT) mode = AI_AGGRESSIVE;                  /* escortee gone */
    } else if (mode == AI_GUARD_POINT || mode == AI_ESCAPE || mode == AI_ESCORT) mode = AI_AGGRESSIVE;

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

    if (src == AI_NO_TARGET) {
        if (s->field_state == 0 || s->field_mtype != mtype || s->field_side != side || s->field_src != AI_NO_TARGET)
            ai_field_request(b, s, mtype, side);
    } else if (s->field_state == 0 || s->field_mtype != mtype || s->field_side != side || s->field_src != src)
        field_request_cell(s, mtype, side, src);           /* replaces the cache (8.4) */
    while (!ai_field_pump(s, 1024)) {}             /* finish synchronously if the phase-start job is not done */
    PROF_SET(dbg_ai_t_field, PROF_T() - pt0); pt0 = PROF_T();
    here_field = fld_get((uint16_t)a.y * grid_w + a.x);

    prepare_special(b, s, &a, actor);

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
                /* leash constraints are filters, never penalties (8.2) */
                if ((mode == AI_GUARD_POINT || mode == AI_ESCORT) && manhattan(ex, ey, gpx, gpy) > leash) continue;
            }
            cell = (uint16_t)ey * grid_w + ex;
            et = grid_terrain(ex, ey) & 0x0F;

            base = -W_MOVECOST * cost;
            if (mode == AI_ESCAPE && ex == gpx && ey == gpy) base += W_MISSION;   /* reaches the exit */
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
                out->param = 0xFF; out->tx = ex; out->ty = ey;
                bcnt = 0; bcost = cost; bcell = cell; have = 1;
            }
            /* STRATEGY candidates: range + LOS from this endpoint */
            for (oi = 0; oi < s->n_strat; oi++) {
                AiStrat *c = &SB[oi];
                uint8_t cx = c->cx == 0xFFu ? ex : c->cx, cy = c->cx == 0xFFu ? ey : c->cy;
                uint8_t d = manhattan(ex, ey, cx, cy), act = (c->sid & 0x80u) ? ACT_ITEM : ACT_STRATEGY;
                int32_t sc;
                if (d > c->range) continue;
                if (c->cx != 0xFFu && cx == a.x && cy == a.y && !(ex == a.x && ey == a.y)) continue;   /* centre moved away */
                sc = base + c->val;
                if (!better(sc, 0, cost, 0xFF, cell, act, out, bcnt, bcost, bcell)) break;   /* sorted: none later wins */
                if (d > 1 && !los_clear(ex, ey, cx, cy)) continue;
                {
                    out->x = ex; out->y = ey; out->action = act; out->target = 0xFF; out->score = sc;
                    out->param = (c->sid & 0x80u) ? (uint8_t)(c->sid & 0x7Fu) : c->sid; out->tx = cx; out->ty = cy;
                    bcnt = 0; bcost = cost; bcell = cell;
                }
                break;                                   /* best legal strategy at this endpoint */
            }
            /* ITEM candidate (self) */
            if (s->item_slot != 0xFF && better(base + s->item_sc, 0, cost, 0xFF, cell, ACT_ITEM, out, bcnt, bcost, bcell)) {
                out->x = ex; out->y = ey; out->action = ACT_ITEM; out->target = 0xFF; out->score = base + s->item_sc;
                out->param = s->item_slot; out->tx = ex; out->ty = ey;
                bcnt = 0; bcost = cost; bcell = cell;
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
                    out->param = 0xFF; out->tx = o->x; out->ty = o->y;
                    bcnt = ce; bcost = cost; bcell = cell;
                }
            }
        }
    }
    PROF_SET(dbg_ai_t_cand, PROF_T() - pt0);
#undef t
}
