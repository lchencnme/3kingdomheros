/* GB platform hooks for the event VM (called from code bank 8: must be NONBANKED / bank 0).
 * Battle flags live in the SRAM12 runtime area (design doc 11.5: offset 6400, 512 B);
 * global flags in WRAM bank1 DF00 (campaign area); the dialogue queue in the event region CC00. */
#include <gb/gb.h>
#include <string.h>
#include "../core/vm.h"
#include "../core/save.h"
#include "../core/item.h"
#include "../gen/tech01_data.h"
#include "../gen/campaign_data.h"
#include "../gen/tech01_events.h"
#include "rom.h"
#include "wram.h"
#include "dialogq.h"
#include "eventram.h"
#include "duel.h"

#define SRAM_RUNTIME_BANK 12u
#define SRAM_BATTLE_FLAGS 6400u
#define W1_CAMPAIGN ((uint8_t *)0xDF00u)

uint8_t cur_bt;                                 /* index into battle_defs / battle_ev (design doc 2.1) */

uint8_t vm_event_count(void) PLATFORM_API { return battle_ev[cur_bt].n_events; }

void vm_event_header(uint8_t i, VmEvent *e) PLATFORM_API { memcpy(e, &battle_ev[cur_bt].events[i], sizeof *e); }

void vm_read(uint16_t off, uint8_t *dst, uint8_t n) PLATFORM_API
{
    rom_copy(TECH01_SCRIPT_BANK, dst, battle_ev[cur_bt].script + off, n);
}

uint8_t vm_flag_get(uint8_t scope, uint16_t id) PLATFORM_API
{
    uint8_t b, o;
    if (scope == SCOPE_BATTLE) {
        sram_read(SRAM_RUNTIME_BANK, (uint16_t)(SRAM_BATTLE_FLAGS + (id >> 3)), &b, 1);
    } else {
        o = SVBK_REG; SVBK_REG = WB_UNITS; b = W1_CAMPAIGN[(id >> 3) & 0xFF]; SVBK_REG = o;
    }
    return (uint8_t)((b >> (id & 7u)) & 1u);
}

void vm_flag_set(uint8_t scope, uint16_t id, uint8_t v) PLATFORM_API
{
    uint8_t b, o, m = (uint8_t)(1u << (id & 7u));
    if (scope == SCOPE_BATTLE) {
        uint16_t off = (uint16_t)(SRAM_BATTLE_FLAGS + (id >> 3));
        sram_read(SRAM_RUNTIME_BANK, off, &b, 1);
        b = v ? (uint8_t)(b | m) : (uint8_t)(b & ~m);
        sram_write(SRAM_RUNTIME_BANK, off, &b, 1);
    } else {
        o = SVBK_REG; SVBK_REG = WB_UNITS;
        if (v) W1_CAMPAIGN[(id >> 3) & 0xFF] |= m; else W1_CAMPAIGN[(id >> 3) & 0xFF] &= (uint8_t)~m;
        SVBK_REG = o;
    }
}

void vm_text(uint8_t speaker, uint16_t text_id) PLATFORM_API
{
    DialogQ *q = DQ;
    if (q->n < DQ_MAX) { q->speaker[q->n] = speaker; q->text[q->n] = text_id; q->n++; }
}

uint8_t vm_template(uint8_t idx, VmTemplate *t) PLATFORM_API
{
    if (idx >= battle_ev[cur_bt].n_templates) return 0;
    memcpy(t, &battle_ev[cur_bt].templates[idx], sizeof *t);
    return 1;
}

uint16_t gold;                                  /* campaign gold (GIVE_GOLD, battle rewards, shop) */
void vm_gold(uint16_t amount) PLATFORM_API { gold = (uint16_t)(gold + amount < gold ? 0xFFFFu : gold + amount); }

/* treasures of the current battle (design doc 7.4) */
uint8_t treasure_def(uint8_t i, TreasureDef *t) PLATFORM_API
{
    const BattleDef *d = &battle_defs[cur_bt];
    if (i >= d->n_tre) return 0;
    t->x = d->tre[i][0]; t->y = d->tre[i][1]; t->item = d->tre[i][2];
    t->flag = (uint16_t)(TECH01_TREASURE_FLAG0 + i);
    return 1;
}

/* called by OP_DUEL after the state change: the queue entry is played in order with the text pages */
void vm_duel(uint8_t winner, uint8_t loser, uint8_t retreated) PLATFORM_API
{
    DialogQ *q = DQ;
    if (q->n < DQ_MAX) { q->speaker[q->n] = DQ_DUEL; q->text[q->n] = (uint16_t)((winner << 8) | loser); q->n++; }
    {
        DuelInfo *d = DUEL_INFO;
        ActiveUnit u;
        d->winner = winner; d->loser = loser; d->retreated = retreated;
        unit_load(winner, &u); d->w_cls = u.class_id; d->w_fac = u.faction; d->w_hp = u.hp;
        unit_load(loser, &u); d->l_cls = u.class_id; d->l_fac = u.faction; d->l_hp = u.hp;
    }
}

void vm_clear_battle_flags(void)
{
    uint8_t z[32];
    uint16_t off;
    memset(z, 0, sizeof z);
    for (off = 0; off < 512u; off += 32u) sram_write(SRAM_RUNTIME_BANK, (uint16_t)(SRAM_BATTLE_FLAGS + off), z, 32);
}

/* campaign area (global flags, design doc 11.5/11.6): WRAM1 DF00 */
void campaign_read(uint8_t off, uint8_t *dst, uint8_t n) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS; memcpy(dst, W1_CAMPAIGN + off, n); SVBK_REG = o;
}

void campaign_write(uint8_t off, const uint8_t *src, uint8_t n) PLATFORM_API
{
    uint8_t o = SVBK_REG;
    SVBK_REG = WB_UNITS; memcpy(W1_CAMPAIGN + off, src, n); SVBK_REG = o;
}
