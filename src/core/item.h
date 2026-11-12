/* Inventory and battle items (design doc 第7章): 8 carry slots per unit, consumables, exchange.
 * Storage: SRAM bank15 0..1919, 96 records x 20 B (8 x u16 item id LE, FFFF = empty; 3 equip; 1 pad),
 * accessed only through sram_read/sram_write (11.5). The battle save copies the block as tag 9. */
#ifndef CORE_ITEM_H
#define CORE_ITEM_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"
#include "rules.h"
#include "strategy.h"

#define INV_SLOTS 8u
#define INV_REC 20u
#define INV_SRAM_BANK 15u
#define ITEM_NONE 0xFFu

#define IR_OK 0u
#define IR_EMPTY 1u        /* no item in that slot */
#define IR_RANGE 2u        /* target out of range */
#define IR_NO_TARGET 3u    /* no valid unit at the target cell */
#define IR_NO_EFFECT 4u    /* full hp / morale / mp, nothing to cleanse, weather already set: not consumed */
#define IR_FULL 5u         /* exchange: receiver has no free slot */
#define IR_EQUIPPED 6u     /* equipped items move only at the camp (unequip first) */
#define IR_CLASS 7u        /* promotion / class change / equipment not allowed for this class or level */

#define EQ_WEAPON 0u
#define EQ_MANUAL 1u
#define EQ_MOUNT 2u
#define EQ_NONE 0xFFu

typedef struct {
    uint8_t item, target, killed;
    uint16_t amount;       /* hp / morale / mp restored, or damage dealt */
} ItemResult;

uint8_t inv_get(uint8_t unit, uint8_t slot) CORE_API;              /* item id or ITEM_NONE */
void inv_set(uint8_t unit, uint8_t slot, uint8_t item) CORE_API;
uint8_t inv_add(uint8_t unit, uint8_t item) CORE_API;              /* slot used, or ITEM_NONE if full */
uint8_t inv_count(uint8_t unit) CORE_API;
void inv_clear_all(void) CORE_API;                                 /* every record empty */

/* validity of using the item in 'slot' of user (standing at its current cell) on cell (tx, ty) */
uint8_t item_check(const Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty) CORE_API;
/* apply (no RNG is drawn except strategy books, no counter, no XP: design doc 7.3); consumes the item and
 * the action. Strategy books (IK_BOOK) must go through item_use_book. */
uint8_t item_use(Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty, ItemResult *r) CORE_API;
/* strategy book: no MP, ignores class/learn level/SILENCE, MAG = ITEM_SCROLL_MAG, no XP; consumed even
 * when it misses (7.3) */
uint8_t item_use_book(Battle *b, uint8_t user, uint8_t slot, uint8_t cx, uint8_t cy, StrategyResult *sr) CORE_API;
/* expected value of an item for AI scoring: amount it would restore/deal on target t (0 = useless) */
uint16_t item_value(const Battle *b, uint8_t item, const ActiveUnit *t) CORE_API;

/* battle exchange (7.3): adjacent allies, actor slot sa <-> receiver slot sb (sb may be empty);
 * consumes the actor's action only; equipped items never move in battle */
uint8_t item_exchange(uint8_t actor, uint8_t sa, uint8_t recv, uint8_t sb) CORE_API;
/* between battles (camp): any two units, no distance, no action; equipped items are unequipped first */
uint8_t item_exchange_camp(uint8_t a, uint8_t sa, uint8_t b, uint8_t sb) CORE_API;

/* equipment (7.1): category of an item (EQ_* or EQ_NONE); equipped slot of a category or ITEM_NONE */
uint8_t item_category(uint8_t item) CORE_API;
/* copy of an item's definition (the table lives in bank 13 with this module) */
void item_def(uint8_t item, ItemDef *out) CORE_API;
uint8_t inv_equipped(uint8_t unit, uint8_t cat) CORE_API;
uint8_t inv_slot_equipped(uint8_t unit, uint8_t slot) CORE_API;
/* camp only: equip the item in slot (replaces the category's previous one) / unequip; stats re-derived */
uint8_t item_equip(uint8_t unit, uint8_t slot) CORE_API;
void item_unequip(uint8_t unit, uint8_t cat) CORE_API;
/* equipment bonuses into the derivation input (called by every stat derivation) */
void item_mods(uint8_t unit, StatIn *s) CORE_API;
/* re-derive unit stats from class/level/base/equipment, keeping the hp/mp deficit (5.4/5.5) */
void item_rederive(uint8_t unit) CORE_API;
/* 青囊书-style heal bonus percent of the unit's equipped manual */
uint8_t item_heal_bonus(uint8_t unit) CORE_API;
/* shop (7.3): sell price floor(price/2); 0 = cannot be sold */
uint16_t item_sell_price(uint8_t item) CORE_API;

/* ---- treasures and the prize queue (7.4) ---- */
typedef struct { uint8_t x, y, item; uint16_t flag; } TreasureDef;   /* flag: battle-scope VM flag "taken" */
uint8_t treasure_def(uint8_t i, TreasureDef *t) PLATFORM_API;         /* 0 = no such treasure */
/* untaken treasure on (x, y), or 0xFF */
uint8_t treasure_at(uint8_t x, uint8_t y) CORE_API;
/* claim treasure i for receiver: IR_FULL if the receiver has no free slot (nothing changes, never swallowed);
 * the taken flag is written before the item is given */
uint8_t treasure_take(uint8_t i, uint8_t receiver) CORE_API;
/* any alive unit of 'faction' with a free slot (first by instance id), or 0xFF */
uint8_t inv_first_free(const Battle *b, uint8_t faction) CORE_API;

#define PRIZE_MAX 16u
#define PRIZE_SRAM_OFF 7872u        /* SRAM12 runtime area (11.5 reserve): count + 16 item ids */
/* scripted rewards (GIVE_ITEM) wait here until distributed, sold or discarded after the battle */
uint8_t prize_push(uint8_t item) CORE_API;            /* 0 = queue full (the script is told, never silent) */
uint8_t prize_count(void) CORE_API;
uint8_t prize_get(uint8_t i) CORE_API;
void prize_remove(uint8_t i) CORE_API;
void prize_clear(void) CORE_API;
#endif
