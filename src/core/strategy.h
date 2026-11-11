/* Strategies (design doc 5.3, 6.3, 6.4): validation, area targets, resolution. No counters. */
#ifndef CORE_STRATEGY_H
#define CORE_STRATEGY_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"

#define SR_OK 0u
#define SR_NO_MP 1u
#define SR_NOT_LEARNED 2u
#define SR_SILENCED 3u
#define SR_RANGE 4u
#define SR_LOS 5u
#define SR_WEATHER 6u
#define SR_NO_TARGET 7u

#define STRAT_MAX_TARGETS 5u

typedef struct {
    uint8_t n;                               /* affected targets (in instance order) */
    uint8_t unit[STRAT_MAX_TARGETS];
    uint8_t hit[STRAT_MAX_TARGETS];          /* 1 if the effect applied */
    uint16_t amount[STRAT_MAX_TARGETS];      /* damage / heal / morale actually applied */
    uint8_t killed[STRAT_MAX_TARGETS];
    uint8_t xp, levels;
} StrategyResult;

/* is strategy sid learned by the caster (class learn mask + learn level)? */
uint8_t strategy_known(const ActiveUnit *c, uint8_t sid) CORE_API;
/* full validity check for casting sid from the caster's cell at center (cx,cy); SR_* code */
uint8_t strategy_check(const Battle *b, uint8_t caster, uint8_t sid, uint8_t cx, uint8_t cy) CORE_API;
/* resolve (caster must already stand at its final cell); returns SR_* (state untouched unless SR_OK) */
uint8_t strategy_cast(Battle *b, uint8_t caster, uint8_t sid, uint8_t cx, uint8_t cy, StrategyResult *r) CORE_API;

/* strategy books (7.3): same resolution without MP/learn/SILENCE checks, MAG = ITEM_SCROLL_MAG, no XP */
uint8_t strategy_book_check(const Battle *b, uint8_t user, uint8_t sid, uint8_t cx, uint8_t cy) CORE_API;
uint8_t strategy_book_cast(Battle *b, uint8_t user, uint8_t sid, uint8_t cx, uint8_t cy, StrategyResult *r) CORE_API;

/* AI estimate (design doc 8.3): expected value of casting sid on centre (cx, cy). Offensive: damage at
 * variance 100 x hit%, clamped to remaining hp, kills, new controls. Heal / morale: amounts actually
 * restored on allies. Ignores the caster's position (the caller checks range and LOS from the endpoint).
 * Returns 0 if not castable / no valid target at the centre. */
typedef struct { uint16_t dmg; uint8_t kills, ctrl; uint16_t heal, morale; } StratEstimate;
uint8_t strategy_estimate(const Battle *b, const ActiveUnit *c, uint8_t caster, uint8_t sid, uint8_t cx, uint8_t cy,
                          StratEstimate *e, ActiveUnit *tmp) CORE_API;
#endif
