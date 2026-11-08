/* Enemy/ally AI (design doc 第8章): candidate generation over every legal endpoint,
 * tiered i32 scoring, deterministic tie-breaks. No RNG is ever drawn. */
#ifndef CORE_AI_H
#define CORE_AI_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"
#include "path.h"

#define AI_HOLD 0u
#define AI_AGGRESSIVE 1u

#define ACT_ATTACK 2u     /* enum order DUEL/TALK/ATTACK/STRATEGY/ITEM/WAIT */
#define ACT_WAIT 5u

#define AI_MAX_OPP 24u
#define AI_MAX_TARGET_CELLS 96u

typedef struct {
    uint8_t id, x, y, move, rmin, rmax, can_counter, kill;
    uint16_t hp;
    uint16_t e_att;           /* actor -> opp expected damage (independent of the endpoint); 0xFFFF = lazy */
    uint16_t threat[10];      /* opp -> actor expected damage by actor terrain; 0xFFFF = lazy.
                                 Also used as the expected counter (approximation: opp at current hp). */
} AiOpp;                      /* 34 bytes */

typedef struct {              /* lives in WRAM bank7 0xDC00..0xDFFF on GB (same bank as MoveRange) */
    uint8_t n_opp;
    AiOpp opp[AI_MAX_OPP];
    uint16_t cells[AI_MAX_TARGET_CELLS];   /* field sources (kept off the 512B stack) */
    uint8_t field_mtype, field_state, field_side;   /* field_state: 0 invalid, 1 building, 2 valid */
    FieldJob job;
} AiScratch;

typedef struct {
    uint8_t x, y, action, target;
    int32_t score;
} AiCommand;

/* invalidate the cached approach field (phase start). Retreats inside a phase keep the stale field:
 * a dead unit's cell stays a source until the next phase (heuristic only, never a rule). */
void ai_invalidate(AiScratch *s) CORE_API;
/* start building the approach field for (mtype, side) as a cooperative job */
void ai_field_request(Battle *b, AiScratch *s, uint8_t mtype, uint8_t side) CORE_API;
/* advance the job by up to 'budget' pops; returns 1 when the field is valid */
uint8_t ai_field_pump(AiScratch *s, uint16_t budget) CORE_API;
/* decide for actor; mr and s must be addressable together (GB: both in bank7) */
void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out) CORE_API;
#endif
