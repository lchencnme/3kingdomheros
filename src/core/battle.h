/* Battle state, 40B active unit records, attack/counter/XP resolution, phases, outcome
 * (design doc 3.3, 4.1, 4.4, 5.4, 6.4-6.6, 附录B.1). Units are only touched through
 * unit_load/unit_store so that on GB they can live in WRAM bank1. */
#ifndef CORE_BATTLE_H
#define CORE_BATTLE_H
#include <stdint.h>
#include "platform.h"
#include "rng.h"
#include "rules.h"

#define MAX_UNITS 96u
#define FAC_PLAYER 0u
#define FAC_ALLY 1u
#define FAC_ENEMY 2u

#define AF_MOVED 0x01u
#define AF_ACTED 0x02u
#define AF_COUNTER 0x04u
#define AF_RETREAT 0x08u
#define AF_DEAD 0x10u
#define AF_AWAY 0x20u

#define ANON_CHARACTER 0xFFFFu
#define NO_ROSTER 0xFFFFu

typedef struct {               /* exactly 附录B.1, 40 bytes, little endian on both GB and host */
    uint16_t character_id;
    uint8_t deployment_id, faction, class_id, level, exp, x, y, move;
    uint16_t hp, max_hp, mp, max_mp, atk, def, mag, res;
    uint8_t morale, agility, status_mask;
    uint8_t ttl[5];            /* ROOT CONFUSE SILENCE POISON BURN */
    uint8_t action_flags, ai_id, appearance_variant, expiry_eligible_mask;
    uint16_t roster_offset;
} ActiveUnit;

typedef struct { uint8_t str, intl, cmd; } UnitBase;

#define OUT_NONE 0u
#define OUT_WIN 1u
#define OUT_LOSE 2u

typedef struct {
    Rng rng;
    uint16_t round, turn_limit;
    uint8_t phase, weather, n_units, leader, outcome;
} Battle;

/* ---- platform hooks (GB: bank1 records + ROM templates; host: arrays) ---- */
void unit_load(uint8_t i, ActiveUnit *u) PLATFORM_API;
void unit_store(uint8_t i, const ActiveUnit *u) PLATFORM_API;
void unit_base(uint8_t i, UnitBase *b) PLATFORM_API;
void grid_occ_set(uint8_t x, uint8_t y, uint8_t side) PLATFORM_API;
void grid_occ_clear(uint8_t x, uint8_t y) PLATFORM_API;

uint8_t unit_alive(const ActiveUnit *u) CORE_API;
uint8_t faction_side(uint8_t faction) CORE_API;              /* SIDE_A / SIDE_E */
uint8_t factions_hostile(uint8_t fa, uint8_t fb) CORE_API;

/* create/refresh derived stats; hp/mp set to max */
void battle_spawn(Battle *b, uint8_t i, uint16_t character, uint8_t cls, uint8_t faction,
                  uint8_t level, uint8_t x, uint8_t y) CORE_API;
void battle_move(uint8_t i, uint8_t x, uint8_t y) CORE_API;

/* can unit u (standing at fx,fy) hit target t with a normal attack? (range + LOS) */
uint8_t battle_in_range(const ActiveUnit *u, uint8_t fx, uint8_t fy, const ActiveUnit *t) CORE_API;

typedef struct {
    uint8_t hit_pct;            /* attacker hit % */
    uint16_t dmg;               /* damage at variance 100 */
    uint8_t counter;            /* defender would counter (if it survives) */
    uint8_t c_hit_pct;
    uint16_t c_dmg;             /* counter damage at variance 100, using defender hp after dmg */
} AttackPreview;

/* attacker a is assumed to stand at (fx,fy) */
void battle_preview(const ActiveUnit *a, uint8_t fx, uint8_t fy, const ActiveUnit *d, uint8_t weather,
                    AttackPreview *p) CORE_API;

typedef struct {
    uint8_t hit, killed, xp, levels;
    uint16_t dmg;
    uint8_t countered, c_hit, c_killed, c_xp, c_levels;
    uint16_t c_dmg;
    uint32_t seed_before, seed_after;
} AttackResult;

/* full transaction of 4.4 (no events yet): a must already be at its final cell */
void battle_attack(Battle *b, uint8_t a, uint8_t d, AttackResult *r) CORE_API;

/* 4.1: reset flags, DOT, facility heal; then outcome check */
void battle_phase_start(Battle *b, uint8_t phase) CORE_API;
/* 4.1: TTL tick; after ENEMY also ROUND_END + round++ */
void battle_phase_end(Battle *b) CORE_API;
/* 6.6 (subset): leader removed -> lose; all enemies removed -> win; round limit -> lose */
uint8_t battle_check_outcome(Battle *b, uint8_t round_end_checkpoint) CORE_API;

/* number of units of a faction that can still act this phase */
uint8_t battle_can_act(const ActiveUnit *u) CORE_API;
#endif
