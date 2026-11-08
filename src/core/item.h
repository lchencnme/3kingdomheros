/* Inventory and battle items (design doc 第7章): 8 carry slots per unit, consumables, exchange.
 * Storage: SRAM bank15 0..1919, 96 records x 20 B (8 x u16 item id LE, FFFF = empty; 3 equip; 1 pad),
 * accessed only through sram_read/sram_write (11.5). The battle save copies the block as tag 9. */
#ifndef CORE_ITEM_H
#define CORE_ITEM_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"

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
/* apply (no RNG is drawn, no counter, no XP: design doc 7.3); consumes the item and the action */
uint8_t item_use(Battle *b, uint8_t user, uint8_t slot, uint8_t tx, uint8_t ty, ItemResult *r) CORE_API;
/* expected value of an item for AI scoring: amount it would restore/deal on target t (0 = useless) */
uint16_t item_value(const Battle *b, uint8_t item, const ActiveUnit *t) CORE_API;

/* battle exchange (7.3): adjacent allies, actor slot sa <-> receiver slot sb (sb may be empty);
 * consumes the actor's action only */
uint8_t item_exchange(uint8_t actor, uint8_t sa, uint8_t recv, uint8_t sb) CORE_API;
#endif
