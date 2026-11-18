#ifdef __SDCC
#pragma bank 13
#endif
#include <string.h>
#include "sram_map.h"
#include "roster.h"
#include "item.h"
#include "save.h"

typedef char roster_rec_is_48[(sizeof(RosterRec) == ROSTER_REC) ? 1 : -1];

static uint16_t rec_off(uint8_t idx) { return (uint16_t)(ROSTER_SRAM_OFF + (uint16_t)idx * ROSTER_REC); }

uint8_t roster_count(void) CORE_API
{
    uint8_t n;
    sram_read(ROSTER_SRAM_BANK, ROSTER_COUNT_OFF, &n, 1);
    return n <= ROSTER_MAX ? n : 0u;
}

static void set_count(uint8_t n) { sram_write(ROSTER_SRAM_BANK, ROSTER_COUNT_OFF, &n, 1); }

void roster_clear(void) CORE_API
{
    uint8_t z[32];
    uint16_t k;
    memset(z, 0, sizeof z);
    for (k = 0; k < 256u; k += 32u) sram_write(ROSTER_SRAM_BANK, (uint16_t)(NAMED_SRAM_OFF + k), z, 32);
    set_count(0);
}

void roster_read(uint8_t idx, RosterRec *r) CORE_API { sram_read(ROSTER_SRAM_BANK, rec_off(idx), (uint8_t *)r, ROSTER_REC); }
void roster_write(uint8_t idx, const RosterRec *r) CORE_API { sram_write(ROSTER_SRAM_BANK, rec_off(idx), (const uint8_t *)r, ROSTER_REC); }

uint8_t named_get(uint16_t character) CORE_API
{
    uint8_t v;
    if (character >= 1024u) return FATE_NONE;
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(NAMED_SRAM_OFF + (character >> 2)), &v, 1);
    return (uint8_t)((v >> ((character & 3u) * 2u)) & 3u);
}

void named_set(uint16_t character, uint8_t fate) CORE_API
{
    uint8_t v, sh;
    if (character >= 1024u) return;
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(NAMED_SRAM_OFF + (character >> 2)), &v, 1);
    sh = (uint8_t)((character & 3u) * 2u);
    v = (uint8_t)((v & ~(3u << sh)) | ((fate & 3u) << sh));
    sram_write(ROSTER_SRAM_BANK, (uint16_t)(NAMED_SRAM_OFF + (character >> 2)), &v, 1);
}

uint8_t roster_find(uint16_t character) CORE_API
{
    uint8_t i, n = roster_count();
    uint8_t c[2];
    for (i = 0; i < n; i++) {
        sram_read(ROSTER_SRAM_BANK, rec_off(i), c, 2);
        if ((uint16_t)(c[0] | ((uint16_t)c[1] << 8)) == character) return i;
    }
    return 0xFF;
}

void roster_set_fate(uint16_t character, uint8_t fate) CORE_API
{
    uint8_t i = roster_find(character);
    if (i != 0xFF) sram_write(ROSTER_SRAM_BANK, (uint16_t)(rec_off(i) + 2u), &fate, 1);   /* RosterRec.fate */
    named_set(character, fate);
}

uint8_t roster_available(uint8_t idx) CORE_API
{
    uint8_t f;
    if (idx >= roster_count()) return 0;
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(rec_off(idx) + 2u), &f, 1);
    return f == FATE_OWNED;
}

uint8_t roster_add(uint16_t character, uint8_t cls, uint8_t level, uint8_t str, uint8_t intl, uint8_t cmd) CORE_API
{
    RosterRec r;
    uint8_t n = roster_count();
    if (n >= ROSTER_MAX || roster_find(character) != 0xFF) return 0xFF;
    memset(&r, 0, sizeof r);
    r.character = character; r.fate = FATE_OWNED; r.cls = cls; r.level = level;
    r.str = str; r.intl = intl; r.cmd = cmd; r.morale = 100;
    memset(r.items, 0xFF, sizeof r.items);
    memset(r.equip, 0xFF, sizeof r.equip);
    roster_write(n, &r);
    set_count((uint8_t)(n + 1u));
    named_set(character, FATE_OWNED);
    return n;
}

uint8_t roster_base(uint16_t roster_offset, UnitBase *b) CORE_API
{
    uint8_t s[3];
    if (roster_offset == NO_ROSTER || roster_offset >= (uint16_t)ROSTER_MAX * ROSTER_REC) return 0;
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(ROSTER_SRAM_OFF + roster_offset + 6u), s, 3);
    b->str = s[0]; b->intl = s[1]; b->cmd = s[2];
    return 1;
}

/* stack-lean (it sits under start_battle in the deepest call chain): only the 14 B head of the record */
void roster_deploy(Battle *b, uint8_t idx, uint8_t inst, uint8_t x, uint8_t y, uint8_t replenish) CORE_API
{
    uint8_t h[14], tail[3], buf[19];
    uint16_t hp, mp;
    ActiveUnit u;
    sram_read(ROSTER_SRAM_BANK, rec_off(idx), h, 14);
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(rec_off(idx) + 45u), tail, 3);
    sram_read(ROSTER_SRAM_BANK, (uint16_t)(rec_off(idx) + 14u), buf, 19);                 /* items + equip */
    sram_write(INV_SRAM_BANK, (uint16_t)(RT_INV_OFF + (uint16_t)inst * INV_REC), buf, 19);
    battle_spawn(b, inst, (uint16_t)(h[0] | ((uint16_t)h[1] << 8)), h[3], FAC_PLAYER, h[4], x, y);
    unit_load(inst, &u);
    u.roster_offset = (uint16_t)idx * ROSTER_REC;       /* base stats now come from the record */
    u.exp = h[5]; u.max_hp = 0;                          /* re-derive below as a fresh unit */
    unit_store(inst, &u);
    item_rederive(inst);                                 /* class + level + base + equipment, full */
    unit_load(inst, &u);
    hp = (uint16_t)(h[10] | ((uint16_t)h[11] << 8)); mp = (uint16_t)(h[12] | ((uint16_t)h[13] << 8));
    if (!replenish && (hp || (tail[0] & ROSTER_RETREATED))) {   /* 2.4 NO_REPLENISH: carry it */
        u.hp = hp < u.max_hp ? hp : u.max_hp;
        u.mp = mp < u.max_mp ? mp : u.max_mp;
        u.morale = h[9];
        if (!u.hp) { u.action_flags |= AF_RETREAT; grid_occ_clear(x, y); }
    } else {
        u.morale = 100;
    }
    unit_store(inst, &u);
}

void roster_sync(uint8_t idx, uint8_t inst) CORE_API
{
    RosterRec r;
    ActiveUnit u;
    roster_read(idx, &r);
    unit_load(inst, &u);
    r.cls = u.class_id; r.level = u.level; r.xp = u.exp;
    r.hp = u.hp; r.mp = u.mp; r.morale = u.morale;
    r.reserved[0] = (u.action_flags & (AF_RETREAT | AF_DEAD)) || !u.hp ? ROSTER_RETREATED : 0u;
    sram_read(INV_SRAM_BANK, (uint16_t)(RT_INV_OFF + (uint16_t)inst * INV_REC), r.items, 16);
    sram_read(INV_SRAM_BANK, (uint16_t)(RT_INV_OFF + (uint16_t)inst * INV_REC + 16u), r.equip, 3);
    roster_write(idx, &r);
}
