/* Roster (design doc 5.6, 11.5, 11.6, 附录B.2): persistent officers between battles.
 * SRAM12 runtime area: records 128 x 48 B @0, named states 1024 x 2 bit @6144, roster count @7890.
 * Record: charID u16, fate, class, L, XP, STR, INT, CMD, morale, hp u16, mp u16 (14 B);
 * items 8 x u16 (16 B); equip 3 x u8; permanent mods 6 x i16 (12 B); reserved 3 B (0). */
#ifndef CORE_ROSTER_H
#define CORE_ROSTER_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"

#define ROSTER_MAX 128u
#define ROSTER_REC 48u
#define ROSTER_SRAM_BANK 12u
#define ROSTER_SRAM_OFF 0u
#define NAMED_SRAM_OFF 6144u
#define ROSTER_COUNT_OFF 7890u

#define ROSTER_RETREATED 0x01u   /* reserved[0] bit0 (v2): retreated in the last battle (NO_REPLENISH, 2.4) */
#define FATE_NONE 0u      /* named states (11.6): not joined / owned / left / dead */
#define FATE_OWNED 1u
#define FATE_LEFT 2u
#define FATE_DEAD 3u

typedef struct {
    uint16_t character;
    uint8_t fate, cls, level, xp, str, intl, cmd, morale;
    uint16_t hp, mp;
    uint8_t items[16], equip[3];
    uint8_t mods[12];      /* 6 x i16 little endian: HP MP ATK DEF MAG RES (unused: 0) */
    uint8_t reserved[3];
} RosterRec;          /* 48 B, little endian, same layout in SRAM and in saves */

void roster_clear(void) CORE_API;                    /* empty roster, all named states FATE_NONE */
uint8_t roster_count(void) CORE_API;
void roster_read(uint8_t idx, RosterRec *r) CORE_API;
void roster_write(uint8_t idx, const RosterRec *r) CORE_API;
/* new officer at level/class with base stats; hp/mp 0 = "full" until first derived; returns index or FF */
uint8_t roster_add(uint16_t character, uint8_t cls, uint8_t level, uint8_t str, uint8_t intl, uint8_t cmd) CORE_API;
uint8_t roster_find(uint16_t character) CORE_API;     /* index or FF */
uint8_t named_get(uint16_t character) CORE_API;
void named_set(uint16_t character, uint8_t fate) CORE_API;

/* put officer idx on the field as instance inst at (x, y): its items/equipment go to the instance's item
 * block, its stats are derived from class/level/base/equipment. replenish = 1 (continuity NORMAL):
 * full hp/mp, morale 100; 0 (NO_REPLENISH): carry hp/mp/morale (2.4) */
void roster_deploy(Battle *b, uint8_t idx, uint8_t inst, uint8_t x, uint8_t y, uint8_t replenish) CORE_API;
/* 5.6 sync: hp/mp/morale/L/XP/class and the item block of instance inst back into officer idx */
void roster_sync(uint8_t idx, uint8_t inst) CORE_API;
/* base stats of the officer an instance belongs to (UnitBase hook helper) */
uint8_t roster_base(uint16_t roster_offset, UnitBase *b) CORE_API;
#endif
