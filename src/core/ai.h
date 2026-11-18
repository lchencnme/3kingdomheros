/* Enemy/ally AI (design doc 第8章): candidate generation over every legal endpoint,
 * tiered i32 scoring, deterministic tie-breaks. No RNG is ever drawn. */
#ifndef CORE_AI_H
#define CORE_AI_H
#include <stdint.h>
#include "sram_map.h"
#include "platform.h"
#include "battle.h"
#include "path.h"

#define AI_HOLD 0u
#define AI_AGGRESSIVE 1u
#define AI_GUARD_POINT 2u
#define AI_ESCORT 3u
#define AI_ESCAPE 4u
#define AI_NO_TARGET 0xFFFFu
#define AIF_ALERT 0x01u       /* instance flag: HOLD unit has been threatened (8.2) */
#define AIF_OVERRIDE 0x80u    /* instance record overrides its group */

/* 8.2: groups 96 x 5 B + instance overrides 96 x 5 B = 960 B in SRAM12 @6912 (runtime area, saved as tag 10) */
typedef struct { uint8_t mode; uint16_t target; uint8_t leash, flags; } AiMode;
#define AI_SRAM_BANK RT_BANK
#define AI_SRAM_GROUPS RT_AI_GROUPS
#define AI_SRAM_INST RT_AI_INST
/* groups: n records of {id, mode, target lo, target hi, leash}; undefined groups = HOLD/FFFF/3 */
void ai_groups_init(const uint8_t *recs, uint8_t n) CORE_API;
/* SET_AI (9.3): subject 0..95 = instance override, 0x8000|g = group; resets the alert bit */
void ai_set(uint16_t subject, uint8_t mode, uint16_t target, uint8_t leash) CORE_API;
/* effective mode of a unit: instance override, else its group (ActiveUnit.ai_id); flags carry the alert bit */
void ai_resolve(uint8_t unit, AiMode *m) CORE_API;
/* HOLD alert (8.2/8.4): exact "can an opponent move + attack my cell" test for a not yet alerted HOLD unit;
 * sets the alert bit. Run once per phase start (opponents only decrease during the phase). mr = scratch. */
uint8_t ai_hold_update(Battle *b, MoveRange *mr, uint8_t unit) CORE_API;

#define ACT_ATTACK 2u     /* enum order DUEL/TALK/ATTACK/STRATEGY/ITEM/WAIT */
#define ACT_STRATEGY 3u
#define ACT_ITEM 4u
#define ACT_WAIT 5u

/* offensive strategy candidates of the current actor, valued once per decision (independent of the
 * endpoint); the endpoint loop only checks range and LOS. GB: WRAM bank7 D500..D5FF. */
typedef struct { uint8_t sid, cx, cy, range; int32_t val; } AiStrat;   /* val: endpoint-independent score
                                                                      sid >= 0x80: item slot (fire pot);
                                                                      cx = 0xFF: centre = the endpoint (self) */
#define AI_MAX_STRAT 32u
#ifdef __SDCC
#define AI_STRAT_BUF ((AiStrat *)0xD500u)
#define AI_TMP_UNIT ((ActiveUnit *)0xD4C0u)    /* bank7 D4C0..D4E7: unit buffer for strategy_estimate */
#else
extern ActiveUnit ai_tmp_host;
#define AI_TMP_UNIT (&ai_tmp_host)
extern AiStrat ai_strat_host[AI_MAX_STRAT];
#define AI_STRAT_BUF ai_strat_host
#endif

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
    uint8_t n_strat, item_slot;                       /* per-decision special candidates (kept off the stack) */
    int32_t item_sc;
    uint16_t field_src;                               /* FFFF = all opponents, else a single source cell */
    FieldJob job;
} AiScratch;

typedef struct {
    uint8_t x, y, action, target;
    uint8_t param, tx, ty;    /* STRATEGY: sid + centre cell; ITEM: inventory slot + target cell */
    int32_t score;
} AiCommand;

/* invalidate the cached approach field (phase start). Retreats inside a phase keep the stale field:
 * a dead unit's cell stays a source until the next phase (heuristic only, never a rule). */
void ai_invalidate(AiScratch *s) CORE_API;
/* start building the approach field for (mtype, side) as a cooperative job */
void ai_field_request(Battle *b, AiScratch *s, uint8_t mtype, uint8_t side) CORE_API;
/* player-visible danger view (8.4, UI only): set the bit (y*w+x) of every cell 'unit' could attack after a
 * legal move (exact SPFA + range + LOS). bits: w*h bits (<= 512 B). mr: scratch */
void ai_danger_add(Battle *b, MoveRange *mr, uint8_t unit, uint8_t *bits) CORE_API;
/* start (as a cooperative job) the approach field this unit's next decision needs, unless cached */
void ai_field_prepare(Battle *b, AiScratch *s, uint8_t unit) CORE_API;
/* advance the job by up to 'budget' pops; returns 1 when the field is valid */
uint8_t ai_field_pump(AiScratch *s, uint16_t budget) CORE_API;
/* decide for actor; mr and s must be addressable together (GB: both in bank7) */
void ai_decide(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, uint8_t mode, AiCommand *out) CORE_API;
/* same with a full mode record (8.2: GUARD_POINT target = cell y*w+x, ESCORT target = unit, ESCAPE target =
 * exit cell, AGGRESSIVE target = unit or FFFF). An alerted HOLD unit acts as AGGRESSIVE. */
void ai_decide_mode(Battle *b, AiScratch *s, MoveRange *mr, uint8_t actor, const AiMode *m, AiCommand *out) CORE_API;
#endif
