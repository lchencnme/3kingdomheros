#ifdef __SDCC
#pragma bank 3
#endif
#include "rules.h"
#include "types.h"

void rules_derive(const StatIn *in, Derived *o) CORE_API
{
    const ClassDef *c = &class_defs[in->cls];
    int32_t lv = (int32_t)in->level - 1;
    int32_t mv;
    o->max_hp = (uint16_t)clamp32((int32_t)c->hp + 12 * lv + 2 * (int32_t)in->cmd + in->mod_hp, 1, HP_MAX);
    o->max_mp = (uint16_t)clamp32((int32_t)c->mp + 2 * lv + in->intl / 5 + in->mod_mp, 0, MP_MAX);
    o->atk = (uint16_t)clamp32((int32_t)c->atk + 2 * lv + in->str / 2 + in->mod_atk, 0, 999);
    o->def = (uint16_t)clamp32((int32_t)c->def + 2 * lv + in->cmd / 2 + in->mod_def, 0, 999);
    o->mag = (uint16_t)clamp32((int32_t)c->mag + 2 * lv + in->intl / 2 + in->mod_mag, 0, 999);
    o->res = (uint16_t)clamp32((int32_t)c->res + 2 * lv + in->intl / 3 + in->mod_res, 0, 999);
    o->agi = (uint8_t)clamp32(((int32_t)in->str + in->cmd) / 4 + c->agi + in->mod_agi, 0, 100);
    mv = (int32_t)c->move + in->mod_move;
    o->move = (uint8_t)clamp32(mv, 0, 9);
}

uint8_t rules_phys_hit(uint8_t agi_a, uint8_t agi_d, uint8_t morale_a, uint8_t terrain_d) CORE_API
{
    int32_t h = 85 + fdiv32((int32_t)agi_a - (int32_t)agi_d, 2) + morale_a / 10
              - terrain_defs[terrain_d].hit_penalty;
    return (uint8_t)clamp32(h, 10, 100);
}

uint16_t rules_phys_damage(const PhysIn *in, uint16_t *trace) CORE_API
{
    int32_t P, R, t;
    uint8_t wea;
    uint32_t hp_ratio = in->max_hp ? ((uint32_t)50 * in->hp) / in->max_hp : 0;
    P = ((int32_t)in->atk * (int32_t)(50 + hp_ratio)) / 100;
    R = 2 * P - (int32_t)in->def;
    t = P / 5;
    if (R < t) R = t;
    if (R < 1) R = 1;
    if (trace) { trace[0] = (uint16_t)P; trace[1] = (uint16_t)R; }
    R = R * type_percent_table[in->fam_a][in->fam_d] / 100;
    if (trace) trace[2] = (uint16_t)R;
    R = R * (75 + in->morale / 2) / 100;
    if (trace) trace[3] = (uint16_t)R;
    R = R * (100 - terrain_defs[in->terrain_d].dr) / 100;
    if (trace) trace[4] = (uint16_t)R;
    wea = (in->rain && in->fam_a == FAM_BOW) ? RAIN_BOW_PERCENT : 100;
    R = R * wea / 100;
    if (trace) trace[5] = (uint16_t)R;
    R = R * in->variance / 100;
    if (trace) trace[6] = (uint16_t)R;
    if (R < 1) R = 1;
    if (R > (int32_t)HP_MAX) R = (int32_t)HP_MAX;
    return (uint16_t)R;
}

uint8_t rules_mag_hit(uint8_t int_a, uint8_t int_d, uint8_t morale_a) CORE_API
{
    int32_t h = 70 + fdiv32((int32_t)int_a - (int32_t)int_d, 2) + morale_a / 10;
    return (uint8_t)clamp32(h, 5, 100);
}

uint8_t rules_element_percent(uint8_t effect, uint8_t terrain_t, uint8_t rain) CORE_API
{
    if (effect == FX_FIRE && terrain_t == TER_2) return 125;   /* fire on forest */
    if (effect == FX_WATER && rain) return 125;                /* water in rain */
    if (effect == FX_ROCK && terrain_t == TER_5) return 125;   /* rock on hill */
    return 100;
}

uint16_t rules_mag_damage(uint8_t power, uint16_t mag, uint16_t res, uint8_t morale,
                          uint8_t elem_percent, uint8_t variance, uint16_t *trace) CORE_API
{
    int32_t R = (int32_t)power + (int32_t)mag - (int32_t)(res / 2);
    if (R < 1) R = 1;
    if (trace) trace[0] = (uint16_t)R;
    R = R * (75 + morale / 2) / 100;
    if (trace) trace[1] = (uint16_t)R;
    R = R * elem_percent / 100;
    if (trace) trace[2] = (uint16_t)R;
    R = R * variance / 100;
    if (trace) trace[3] = (uint16_t)R;
    if (R < 1) R = 1;
    if (R > (int32_t)HP_MAX) R = (int32_t)HP_MAX;
    return (uint16_t)R;
}

uint8_t rules_xp_hit(uint8_t level_self, uint8_t level_target, uint8_t hit, uint8_t killed) CORE_API
{
    int32_t x;
    if (!hit) return 1;
    x = clamp32(10 + 2 * ((int32_t)level_target - (int32_t)level_self), 1, 30);
    if (killed) x += 20;
    return (uint8_t)x;
}

uint8_t rules_xp_heal(uint16_t actual, uint16_t target_max_hp) CORE_API
{
    if (actual == 0 || target_max_hp == 0) return 0;
    return (uint8_t)clamp32((int32_t)(((uint32_t)actual * 10u) / target_max_hp), 1, 10);
}

uint8_t rules_add_xp(uint8_t *level, uint8_t *xp, uint16_t gain) CORE_API
{
    uint8_t ups = 0;
    uint16_t total;
    if (*level >= LEVEL_MAX) { *xp = 0; return 0; }
    total = (uint16_t)*xp + gain;
    while (total >= 100 && *level < LEVEL_MAX) {
        total -= 100;
        (*level)++;
        ups++;
    }
    *xp = (*level >= LEVEL_MAX) ? 0 : (uint8_t)total;
    return ups;
}

uint16_t rules_keep_deficit(uint16_t old_max, uint16_t old_cur, uint16_t new_max, uint16_t lo) CORE_API
{
    int32_t v = (int32_t)new_max - ((int32_t)old_max - (int32_t)old_cur);
    return (uint16_t)clamp32(v, lo, new_max);
}

uint16_t rules_percent_ceil(uint16_t max_hp, uint8_t pct) CORE_API
{
    uint32_t v = ((uint32_t)max_hp * pct + 99u) / 100u;
    return (uint16_t)(v < 1 ? 1 : v);
}

uint8_t rules_class_move_type(uint8_t cls) CORE_API { return class_defs[cls].move_type; }
uint8_t rules_terrain_dr(uint8_t terrain) CORE_API { return terrain_defs[terrain].dr; }
