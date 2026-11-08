/* Enemy/ally AI (design doc 第8章): candidate generation over every legal endpoint,
 * tiered i32 scoring, deterministic tie-breaks. No RNG is ever drawn. */
#ifndef CORE_AI_H
#define CORE_AI_H
#include <stdint.h>
#include "battle.h"
#include "path.h"

#define AI_HOLD 0u
#define AI_AGGRESSIVE 1u

#define ACT_ATTACK 2u     /* enum order DUEL/TALK/ATTACK/STRATEGY/ITEM/WAIT */
#define ACT_WAIT 5u

#define AI_MAX_OPP 24u
#define AI_MAX_TARGET_CELLS 96u

typedef struct {
    uint8_t id, x, y, cls, move, rmax, terrain;
    uint16_t hp;
    uint16_t threat[10];      /* expected dmg vs actor by actor terrain; 0xFFFF = not computed */
} AiOpp;                      /* 28 bytes */

typedef struct {              /* lives in WRAM bank7 0xDC00..0xDFFF on GB (same bank as MoveRange) */
    uint8_t n_opp;
    AiOpp opp[AI_MAX_OPP];
    uint16_t cells[AI_MAX_TARGET_CELLS];   /* field sources (kept off the 512B stack) */
    uint8_t field_mtype, field_valid, field_side;
    uint16_t field_epoch;
} AiScratch;

typedef struct {
    uint8_t x, y, action, target;
    int32_t score;
} AiCommand;

/* invalidate the cached approach field (call at phase start and when any unit retreats/moves sides) */
void ai_invalidate(AiScratch *s);
/* decide for actor; mr and s must be addressable together (GB: both in bank7) */
void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out);
#endif
