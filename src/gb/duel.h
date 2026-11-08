/* queued duel presentation data (event region ER_DUEL, 14 B): written by the bank-0 VM hook before
 * the duel changes state, played by main.c in queue order with the text pages */
#ifndef GB_DUEL_H
#define GB_DUEL_H
#include <stdint.h>
#include "eventram.h"
typedef struct {
    uint8_t winner, loser, retreated, w_cls, w_fac, l_cls, l_fac, pad;
    uint16_t w_hp, l_hp;
} DuelInfo;
#define DUEL_INFO ((DuelInfo *)ER_DUEL)
#endif
