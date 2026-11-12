#ifdef __SDCC
#pragma bank 4
#endif
#include "battle.h"
#include "item.h"
#include "grid.h"
#include "path.h"
#include "types.h"

typedef char active_unit_is_40_bytes[(sizeof(ActiveUnit) == 40) ? 1 : -1];

uint8_t unit_alive(const ActiveUnit *u) CORE_API
{
    return u->hp > 0 && !(u->action_flags & (AF_RETREAT | AF_DEAD | AF_AWAY));
}

uint8_t faction_side(uint8_t faction) CORE_API { return faction == FAC_ENEMY ? SIDE_E : SIDE_A; }

uint8_t factions_hostile(uint8_t fa, uint8_t fb) CORE_API { return faction_side(fa) != faction_side(fb); }

static void derive_into(uint8_t i, ActiveUnit *u)
{
    StatIn s;
    Derived d;
    UnitBase base;
    unit_base(i, &base);
    s.cls = u->class_id; s.level = u->level; s.str = base.str; s.intl = base.intl; s.cmd = base.cmd;
    s.mod_hp = s.mod_mp = s.mod_atk = s.mod_def = s.mod_mag = s.mod_res = 0;
    s.mod_agi = 0; s.mod_move = 0;
    item_mods(i, &s);                     /* equipment bonuses (7.1) */
    rules_derive(&s, &d);
    u->max_hp = d.max_hp; u->max_mp = d.max_mp; u->atk = d.atk; u->def = d.def;
    u->mag = d.mag; u->res = d.res; u->agility = d.agi; u->move = d.move;
}

void battle_spawn(Battle *b, uint8_t i, uint16_t character, uint8_t cls, uint8_t faction,
                  uint8_t level, uint8_t x, uint8_t y) CORE_API
{
    ActiveUnit u;
    uint8_t k;
    u.character_id = character; u.deployment_id = i; u.faction = faction; u.class_id = cls;
    u.level = level; u.exp = 0; u.x = x; u.y = y;
    u.morale = 100; u.status_mask = 0;
    for (k = 0; k < 5; k++) u.ttl[k] = 0;
    u.action_flags = 0; u.ai_id = 0; u.appearance_variant = 0; u.expiry_eligible_mask = 0;
    u.roster_offset = NO_ROSTER;
    unit_store(i, &u);          /* base stats may be looked up by index */
    derive_into(i, &u);
    u.hp = u.max_hp; u.mp = u.max_mp;
    unit_store(i, &u);
    grid_occ_set(x, y, faction_side(faction));
    if (i >= b->n_units) b->n_units = (uint8_t)(i + 1);
}

void battle_move(uint8_t i, uint8_t x, uint8_t y) CORE_API
{
    ActiveUnit u;
    unit_load(i, &u);
    grid_occ_clear(u.x, u.y);
    u.x = x; u.y = y;
    u.action_flags |= AF_MOVED;
    grid_occ_set(x, y, faction_side(u.faction));
    unit_store(i, &u);
}

static uint8_t manhattan(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t dx = x0 > x1 ? x0 - x1 : x1 - x0, dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return (uint8_t)(dx + dy);
}

uint8_t battle_in_range(const ActiveUnit *u, uint8_t fx, uint8_t fy, const ActiveUnit *t) CORE_API
{
    const ClassDef *c = &class_defs[u->class_id];
    uint8_t d = manhattan(fx, fy, t->x, t->y);
    if (c->range_max == 0 || d < c->range_min || d > c->range_max) return 0;
    if (d > 1 && !los_clear(fx, fy, t->x, t->y)) return 0;
    return 1;
}

static uint16_t calc_dmg(const ActiveUnit *a, uint16_t a_hp, const ActiveUnit *d, uint8_t d_terrain,
                         uint8_t weather, uint8_t variance)
{
    PhysIn in;
    in.atk = a->atk; in.def = d->def; in.hp = a_hp; in.max_hp = a->max_hp;
    in.fam_a = class_defs[a->class_id].family; in.fam_d = class_defs[d->class_id].family;
    in.morale = a->morale; in.terrain_d = d_terrain; in.rain = weather; in.variance = variance;
    return rules_phys_damage(&in, 0);
}

static uint8_t can_counter(const ActiveUnit *d, const ActiveUnit *a_at)
{
    if (!unit_alive(d)) return 0;
    if (d->status_mask & (1u << ST_CONFUSE)) return 0;
    if (d->action_flags & AF_COUNTER) return 0;
    if (!class_defs[d->class_id].counter) return 0;
    return battle_in_range(d, d->x, d->y, a_at);
}

static ActiveUnit pv_a_at;   /* static: keeps battle_preview's 40B copy off the stack */

void battle_preview(const ActiveUnit *a, uint8_t fx, uint8_t fy, const ActiveUnit *d, uint8_t weather,
                    AttackPreview *p) CORE_API
{
#define a_at pv_a_at
    a_at = *a;
    uint8_t td = grid_terrain(d->x, d->y) & 0x0F, ta = grid_terrain(fx, fy) & 0x0F;
    uint16_t dhp;
    a_at.x = fx; a_at.y = fy;
    p->hit_pct = rules_phys_hit(a->agility, d->agility, a->morale, td);
    p->dmg = calc_dmg(a, a->hp, d, td, weather, 100);
    dhp = p->dmg >= d->hp ? 0 : (uint16_t)(d->hp - p->dmg);
    p->counter = dhp > 0 && can_counter(d, &a_at);
    p->c_hit_pct = p->counter ? rules_phys_hit(d->agility, a->agility, d->morale, ta) : 0;
    p->c_dmg = p->counter ? calc_dmg(d, dhp, &a_at, ta, weather, 100) : 0;
#undef a_at
}

static void retreat(ActiveUnit *u)
{
    uint8_t k;
    grid_occ_clear(u->x, u->y);
    u->hp = 0;
    u->action_flags |= AF_RETREAT;
    u->action_flags &= (uint8_t)~AF_COUNTER;
    u->status_mask = 0;
    for (k = 0; k < 5; k++) u->ttl[k] = 0;
}

static uint8_t give_xp(uint8_t i, ActiveUnit *u, uint16_t xp)
{
    uint8_t ups, old_level = u->level;
    uint16_t old_mhp = u->max_hp, old_mmp = u->max_mp;
    if (!unit_alive(u)) return 0;
    ups = rules_add_xp(&u->level, &u->exp, xp);
    if (ups) {
        derive_into(i, u);
        u->hp = rules_keep_deficit(old_mhp, u->hp, u->max_hp, 1);
        u->mp = rules_keep_deficit(old_mmp, u->mp, u->max_mp, 0);
    }
    (void)old_level;
    return ups;
}

/* one strike: 2 draws always */
static void strike(Battle *b, ActiveUnit *a, ActiveUnit *d, uint8_t *hit, uint16_t *dmg, uint8_t *killed)
{
    uint8_t td = grid_terrain(d->x, d->y) & 0x0F;
    uint8_t pct = rules_phys_hit(a->agility, d->agility, a->morale, td);
    uint16_t roll = rng_rand(&b->rng, 100);
    uint8_t var = (uint8_t)(90 + rng_rand(&b->rng, 21));
    *hit = roll < pct;
    *dmg = 0; *killed = 0;
    if (*hit) {
        *dmg = calc_dmg(a, a->hp, d, td, b->weather, var);
        if (*dmg >= d->hp) { *dmg = d->hp; retreat(d); *killed = 1; }
        else d->hp = (uint16_t)(d->hp - *dmg);
    }
}

void battle_attack(Battle *b, uint8_t ai, uint8_t di, AttackResult *r) CORE_API
{
    ActiveUnit a, d;
    unit_load(ai, &a);
    unit_load(di, &d);
    r->seed_before = b->rng.state;
    r->countered = r->c_hit = r->c_killed = r->c_xp = r->c_levels = 0;
    r->c_dmg = 0;

    strike(b, &a, &d, &r->hit, &r->dmg, &r->killed);
    r->xp = rules_xp_hit(a.level, d.level, r->hit, r->killed);
    r->levels = give_xp(ai, &a, r->xp);

    if (!r->killed && b->outcome == OUT_NONE && can_counter(&d, &a)) {
        /* victory sealing happens before counters: check with the defender still alive */
        r->countered = 1;
        d.action_flags |= AF_COUNTER;
        strike(b, &d, &a, &r->c_hit, &r->c_dmg, &r->c_killed);
        r->c_xp = rules_xp_hit(d.level, a.level, r->c_hit, r->c_killed);
        r->c_levels = give_xp(di, &d, r->c_xp);
    }
    a.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);
    unit_store(ai, &a);
    unit_store(di, &d);
    r->seed_after = b->rng.state;
    battle_check_outcome(b, 0);
}

uint8_t battle_can_act(const ActiveUnit *u) CORE_API
{
    return unit_alive(u) && !(u->action_flags & AF_ACTED) && !(u->status_mask & (1u << ST_CONFUSE));
}

void battle_phase_start(Battle *b, uint8_t phase) CORE_API
{
    uint8_t i;
    ActiveUnit u;
    b->phase = phase;
    for (i = 0; i < b->n_units; i++) {
        unit_load(i, &u);
        if (u.faction != phase || !unit_alive(&u)) continue;
        u.action_flags &= (uint8_t)~(AF_MOVED | AF_ACTED | AF_COUNTER);
        u.expiry_eligible_mask = u.status_mask;
        unit_store(i, &u);
    }
    /* DOT: poison then burn, in instance order */
    for (i = 0; i < b->n_units; i++) {
        uint16_t dmg = 0;
        unit_load(i, &u);
        if (u.faction != phase || !unit_alive(&u)) continue;
        if (u.status_mask & (1u << ST_POISON)) dmg += rules_percent_ceil(u.max_hp, 10);
        if (u.status_mask & (1u << ST_BURN)) dmg += rules_percent_ceil(u.max_hp, 8);
        if (!dmg) continue;
        if (dmg >= u.hp) retreat(&u); else u.hp = (uint16_t)(u.hp - dmg);
        unit_store(i, &u);
    }
    /* facility heal for survivors */
    for (i = 0; i < b->n_units; i++) {
        uint8_t pct;
        unit_load(i, &u);
        if (u.faction != phase || !unit_alive(&u)) continue;
        pct = terrain_defs[grid_terrain(u.x, u.y) & 0x0F].heal_percent;
        if (!pct || u.hp >= u.max_hp) continue;
        u.hp = (uint16_t)(u.hp + rules_percent_ceil(u.max_hp, pct));
        if (u.hp > u.max_hp) u.hp = u.max_hp;
        unit_store(i, &u);
    }
    battle_check_outcome(b, 0);
}

void battle_phase_end(Battle *b) CORE_API
{
    uint8_t i, k;
    ActiveUnit u;
    for (i = 0; i < b->n_units; i++) {
        unit_load(i, &u);
        if (u.faction != b->phase || !unit_alive(&u)) continue;
        for (k = 0; k < 5; k++) {
            uint8_t m = (uint8_t)(1u << k);
            if ((u.expiry_eligible_mask & m) && (u.status_mask & m)) {
                if (--u.ttl[k] == 0) u.status_mask &= (uint8_t)~m;
            }
        }
        u.expiry_eligible_mask = 0;
        unit_store(i, &u);
    }
    if (b->phase == FAC_ENEMY) {
        battle_check_outcome(b, 1);
        if (b->outcome == OUT_NONE) b->round++;
    } else {
        battle_check_outcome(b, 0);
    }
}

/* stack-lean (it runs at the bottom of the attack / strategy / script chains): no 40 B unit copies */
uint8_t battle_check_outcome(Battle *b, uint8_t round_end_checkpoint) CORE_API
{
    uint8_t i, enemies = 0;
    if (b->outcome != OUT_NONE) return b->outcome;
    if (!(unit_state_bits(b->leader) & USB_ALIVE)) { b->outcome = OUT_LOSE; return b->outcome; }   /* leader loss first */
    if (b->protect != NO_UNIT && b->protect < b->n_units && !(unit_state_bits(b->protect) & USB_ALIVE)) {
        b->outcome = OUT_LOSE; return b->outcome;                          /* 6.6: a protected NPC fell */
    }
    for (i = 0; i < b->n_units; i++)
        if ((unit_state_bits(i) & (USB_ALIVE | USB_ENEMY)) == (USB_ALIVE | USB_ENEMY)) enemies++;
    if (!enemies) { b->outcome = OUT_WIN; return b->outcome; }
    if ((b->vic_kind == VIC_DEFEAT_UNIT || b->vic_kind == VIC_SURVIVE) && b->vic_unit != NO_UNIT && b->vic_unit < b->n_units
        && (unit_state_bits(b->vic_unit) & (USB_ALIVE | USB_SPAWNED)) == USB_SPAWNED) {
        b->outcome = OUT_WIN; return b->outcome;                           /* the target general is gone */
    }
    if (b->vic_kind == VIC_REACH && b->vic_unit < b->n_units && (unit_state_bits(b->vic_unit) & USB_ALIVE)) {
        uint8_t x, y;
        unit_xy(b->vic_unit, &x, &y);
        if (x >= b->vic_x0 && x <= b->vic_x1 && y >= b->vic_y0 && y <= b->vic_y1) { b->outcome = OUT_WIN; return b->outcome; }
    }
    if (round_end_checkpoint && b->vic_kind == VIC_SURVIVE && b->round >= b->vic_round) { b->outcome = OUT_WIN; return b->outcome; }
    if (round_end_checkpoint && b->round >= b->turn_limit) b->outcome = OUT_LOSE;
    return b->outcome;
}
