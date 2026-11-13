/* Event VM (design doc 第9章, G1 subset). Events are compiled by tools/gen_events.py into a script blob;
 * the platform provides the blob (GB: copied from ROM through the gateway), flags, text output, templates. */
#ifndef CORE_VM_H
#define CORE_VM_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"

/* triggers (design doc 9.1) */
#define TR_BATTLE_START 0u
#define TR_PHASE_START 1u
#define TR_POST_MOVE 2u
#define TR_PRE_ACTION 3u
#define TR_UNIT_RETREAT 4u
#define TR_ROUND_END 5u
#define TR_PHASE_END 6u
#define TR_BATTLE_END 7u
#define TR_TALK 8u           /* param = actor; conditions usually target_eq (design doc 9.1) */

/* condition nodes */
#define CN_ALL 0x01u
#define CN_ANY 0x02u
#define CN_NOT 0x03u
#define CN_FLAG_EQ 0x10u      /* scope u8, id u16, val u8 */
#define CN_TURN_CMP 0x11u     /* op u8, n u16 */
#define CN_UNIT_ALIVE 0x12u   /* unit u8 */
#define CN_UNIT_IN_RECT 0x13u /* unit u8, x0 y0 x1 y1 */
#define CN_ACTOR_EQ 0x14u     /* unit u8 */
#define CN_TARGET_EQ 0x15u    /* unit u8 */
#define CN_PHASE_EQ 0x16u     /* phase u8 */
#define CMP_EQ 0u
#define CMP_NE 1u
#define CMP_LT 2u
#define CMP_LE 3u
#define CMP_GT 4u
#define CMP_GE 5u

/* opcodes */
#define OP_END 0x01u
#define OP_TEXT 0x02u         /* speaker u8 (portrait index, FF narrator), text u16 */
#define OP_SET_FLAG 0x03u     /* scope u8, id u16, val u8 */
#define OP_IF 0x04u           /* cond u16 (offset), skip u16 (bytes, compiler-checked boundary, forward only) */
#define OP_SPAWN 0x05u        /* template u8, x u8, y u8 */
#define OP_RETREAT 0x06u      /* unit u8 */
#define OP_WEATHER 0x07u      /* CLEAR 0 / RAIN 1 */
#define OP_OUTCOME 0x08u      /* OUT_WIN / OUT_LOSE */
#define OP_GIVE_GOLD 0x09u    /* amount u16 */
#define OP_HP_SET 0x0Au       /* unit u8, value u16 */
#define OP_DUEL 0x0Bu         /* winner u8, loser u8, loser_hp u16: replaces the pending normal attack, 0 draws */
#define OP_GIVE_ITEM 0x0Eu     /* item u8: scripted reward into the prize queue (7.4) */
#define OP_SET_AI 0x0Du       /* subject u16 (0..95 instance, 0x8000|g group), mode u8, target u16, leash u8 */
#define OP_JOIN 0x0Fu         /* joiner u8: officer from the join table enters the roster (no-op if already in) */
#define OP_FATE 0x10u         /* character u16, fate u8 (FATE_OWNED / FATE_LEFT): party leave / return */
#define OP_FACTION_SET 0x0Cu  /* unit u8, faction u8: clears the unit's action for this phase (9.3) */

#define SCOPE_GLOBAL 0u
#define SCOPE_BATTLE 1u
#define VM_ANY 0xFFu
#define VM_NO_ONCE 0xFFFFu

#define VM_OK 0u
#define VM_BAD_SCRIPT 1u
#define VM_BUDGET 2u
#define VM_UNDO_CAP 3u        /* the undo log is full (SCRIPT_UNDO_CAP): the event was rolled back */

typedef struct {               /* compiled event header, 12 bytes */
    uint8_t trigger, param, priority, reserved;
    uint16_t once_bit, cond_off, code_off, code_len;
} VmEvent;

typedef struct {
    uint8_t trigger, phase, actor, target, unit;
} VmFact;

typedef struct {               /* SPAWN template (design doc 9.4) */
    uint16_t character;
    uint8_t cls, level, faction, instance_id;
} VmTemplate;

/* ---- platform hooks ---- */
uint8_t vm_event_count(void) PLATFORM_API;
void vm_event_header(uint8_t i, VmEvent *e) PLATFORM_API;
void vm_read(uint16_t off, uint8_t *dst, uint8_t n) PLATFORM_API;   /* script blob bytes */
uint8_t vm_flag_get(uint8_t scope, uint16_t id) PLATFORM_API;
void vm_flag_set(uint8_t scope, uint16_t id, uint8_t v) PLATFORM_API;
void vm_text(uint8_t speaker, uint16_t text_id) PLATFORM_API;      /* queue a dialogue page */
uint8_t vm_template(uint8_t idx, VmTemplate *t) PLATFORM_API;      /* 0 = no such template */
void vm_gold(uint16_t amount) PLATFORM_API;

void vm_duel(uint8_t winner, uint8_t loser, uint8_t loser_retreated) PLATFORM_API;   /* before the change */
uint8_t vm_join(uint8_t joiner) PLATFORM_API;           /* 0 = no such joiner; already in the roster = 1 */

/* how many events would run for this fact (matching, condition true, once unused); no side effects */
uint8_t vm_would_fire(const Battle *b, const VmFact *f) CORE_API;

/* run every event whose trigger/param/condition matches the fact (priority asc, then id asc).
 * Returns VM_OK, or an error after which the battle must stop safely (design doc 9.3). */
uint8_t vm_fire(Battle *b, const VmFact *f) CORE_API;
/* number of events executed by the last vm_fire (for tests) */
extern uint8_t vm_last_ran;
/* set by the last vm_fire: a DUEL replaced the pending action; units were spawned/removed/converted */
extern uint8_t vm_action_replaced, vm_units_changed;
#endif
