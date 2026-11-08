/* DEFAULT_D1 rev2 formulas (design doc 第5章). Pure functions: preview, AI and resolution share these. */
#ifndef CORE_RULES_H
#define CORE_RULES_H
#include <stdint.h>
#include "platform.h"
#include "../gen/rules_data.h"

typedef struct {
    uint8_t cls, level, str, intl, cmd;
    int16_t mod_hp, mod_mp, mod_atk, mod_def, mod_mag, mod_res;
    int8_t mod_agi, mod_move;
} StatIn;

typedef struct {
    uint16_t max_hp, max_mp, atk, def, mag, res;
    uint8_t agi, move;
} Derived;

void rules_derive(const StatIn *in, Derived *out) CORE_API;

typedef struct {
    uint16_t atk, def, hp, max_hp;  /* attacker ATK/hp/maxHP, defender DEF */
    uint8_t fam_a, fam_d;           /* class families (type triangle) */
    uint8_t morale;                 /* attacker morale */
    uint8_t terrain_d;              /* defender terrain id */
    uint8_t rain;                   /* weather is RAIN */
    uint8_t variance;               /* 90..110 from rand(21)+90 */
} PhysIn;

/* hit% for physical attacks */
uint8_t rules_phys_hit(uint8_t agi_a, uint8_t agi_d, uint8_t morale_a, uint8_t terrain_d) CORE_API;
/* damage on hit (>=1). trace (may be NULL) receives 7 values: P, R, after type, morale, terrain, weather, variance */
uint16_t rules_phys_damage(const PhysIn *in, uint16_t *trace) CORE_API;

uint8_t rules_mag_hit(uint8_t int_a, uint8_t int_d, uint8_t morale_a) CORE_API;
/* element terrain/weather percent for strategy effect against target terrain */
uint8_t rules_element_percent(uint8_t effect, uint8_t terrain_t, uint8_t rain) CORE_API;
/* trace (may be NULL) receives 4 values: base, morale, element, variance */
uint16_t rules_mag_damage(uint8_t power, uint16_t mag, uint16_t res, uint8_t morale,
                          uint8_t elem_percent, uint8_t variance, uint16_t *trace) CORE_API;

/* 5.4 experience */
uint8_t rules_xp_hit(uint8_t level_self, uint8_t level_target, uint8_t hit, uint8_t killed) CORE_API;
uint8_t rules_xp_heal(uint16_t actual, uint16_t target_max_hp) CORE_API;
/* adds xp, handles multi-level-ups and the L99 cap; returns number of levels gained */
uint8_t rules_add_xp(uint8_t *level, uint8_t *xp, uint16_t gain) CORE_API;
/* keep the "deficit" when a maximum changes (level up / promotion); lo is 1 for HP, 0 for MP */
uint16_t rules_keep_deficit(uint16_t old_max, uint16_t old_cur, uint16_t new_max, uint16_t lo) CORE_API;

/* accessors for code outside bank 3 (the tables live in bank 3 with the core) */
uint8_t rules_class_move_type(uint8_t cls) CORE_API;
uint8_t rules_terrain_dr(uint8_t terrain) CORE_API;

/* ceil(max_hp * pct / 100), at least 1: used for facility heal and DOT */
uint16_t rules_percent_ceil(uint16_t max_hp, uint8_t pct) CORE_API;
#endif
