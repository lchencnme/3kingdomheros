/* shared state of the game flow (main.c bank 5, flow.c bank 14) */
#ifndef GB_GAME_H
#define GB_GAME_H
/* 《三国豪杰传》 TECH01 v2 (design doc 16.2 G1 slice).
 * Player phase: select -> move preview -> action menu (攻击/待命) -> target -> resolve.
 * Enemy phase: core AI (第8章) per unit with map-fast presentation. Victory/defeat + restart.
 * Temporary: units are OBJ placeholders (BG compositor pending); presentation is map-fast only. */
#include <gb/gb.h>
#include "debug.h"
#include <gb/cgb.h>
#include <string.h>
#include "../core/battle.h"
#include "../core/ai.h"
#include "../core/path.h"
#include "../core/save.h"
#include "../core/rules.h"
#include "wram.h"
#include "input.h"
#include "mapview.h"
#include "hud.h"
#include "rom.h"
#include "audio.h"
#include "fs.h"
#include "dialog.h"
#include "dialogq.h"
#include "camp.h"
#include "duel.h"
#include "../core/vm.h"
#include "../core/strategy.h"
#include "../core/item.h"
#include "../core/roster.h"
#include "../gen/campaign_data.h"
#include "../gen/tech01_data.h"
#include "../gen/text_data.h"

#define W7_AI ((AiScratch *)0xDC00)
typedef char ai_fits_bank7[(sizeof(AiScratch) <= 0x400u) ? 1 : -1];

void units_clear(void);
void units_snapshot(uint8_t n);
#define MAX_SHOWN 12u               /* OBJ placeholders: up to 12 units (24 OBJ + cursor) */
extern uint8_t snap_x[MAX_SHOWN], snap_y[MAX_SHOWN], snap_fac[MAX_SHOWN], snap_flags[MAX_SHOWN];
extern uint16_t snap_hp[MAX_SHOWN];
void vm_clear_battle_flags(void);
#define SNAP_ALIVE(i) (snap_hp[i] && !(snap_flags[i] & (AF_RETREAT | AF_DEAD | AF_AWAY)))

enum {
    G_IDLE, G_MOVE, G_MENU, G_TARGET, G_SHOW, G_ENEMY, G_BANNER, G_OUTCOME, G_TITLE, G_SLIST, G_SAIM,
    G_ILIST, G_IAIM, G_XLIST, G_CAMP, G_SHOP, G_XRECV, G_DANGER
};

#define MI_ATTACK 0u
#define MI_STRAT 1u
#define MI_WAIT 2u
#define MI_TALK 3u
#define MI_ITEM 4u
#define MI_EXCH 5u
#define WIN_GOLD 500u
#define talks ((uint8_t *)ER_TALKS)
/* UI scratch in the event region (CD00..CD1F): known strategy ids for the strategy list */
#include "eventram.h"
#define SLIST ((uint8_t *)ER_SLIST)
#define targets ((uint8_t *)ER_TARGETS)

#define OPT_AUTOSAVE 0x01u        /* design doc 11.6: autosave at player phase start, default on */

#define W7_DANGER ((uint8_t *)0xDC00u)          /* overlays the AI scratch: invalidate when hiding */  /* battle exchange (7.3): adjacent allies, chosen one, its slot */
extern uint16_t gold;                          /* vm_gb.c (GIVE_GOLD) */
extern uint8_t cur_bt;                         /* vm_gb.c: current battle (battle_defs index) */
#define BD (&battle_defs[cur_bt])
void unit_base_cache_clear(void);
static const uint8_t blank16[16];
/* HUD blank tile: VRAM bank1 id 120 (bank1 72..119 / 200..247 are map slots, 0..71 / 128..199 the HUD
 * pages). It used to be bank0 id 40, which the BG compositor reuses as map slot 40. */
#define HUD_BLANK 120u

/* debug/test hooks (read by tests/emu/test_rom.py through the .noi symbol file) */

extern Battle B;
extern uint8_t state, sel, cur_x, cur_y, tent_x, tent_y, menu, ntarget, tsel, timer, next_state;
extern uint8_t xr_list[4], n_xr, xri, rslot;
extern uint8_t danger_on, dz_i;
extern uint8_t mitems[6], isel, n_mitems, n_slist, ssel, aim_x, aim_y, aim_dirty, n_talk;
extern uint8_t enemy_i, options, autosave_pending, pending_start, end_fired;
extern uint8_t hud_dirty, last_hx, last_hy, hud_settle;
extern uint16_t carry_round;
extern uint8_t camp_save_ok;
extern uint8_t audio_ready;
extern CampDeploy deploy;
extern AiMode ai_mode;
/* functions shared by main.c and flow.c (banked: they live in different banks) */
void attack_and_present(uint8_t ai, uint8_t di, AttackResult *r) BANKED;
void battle_won(void) BANKED;
void begin_enemy_phase(void) BANKED;
void danger_hide(void) BANKED;
uint8_t do_suspend_load(void) BANKED;
uint8_t do_suspend_save(void) BANKED;
void enemy_step(void) BANKED;
void fire(uint8_t trig, uint8_t phase, uint8_t actor, uint8_t target, uint8_t unit) BANKED;
void hud_banner(uint8_t phase) BANKED;
void hud_item_result(const ItemResult *r) BANKED;
void hud_result(const AttackResult *r) BANKED;
void hud_strat_result(uint8_t sid, const StrategyResult *sr) BANKED;
void load_camp_and_go(void) BANKED;
void map_mode_restore(void) BANKED;
void new_game(void) BANKED;
void play_queue(void) BANKED;
uint8_t player_can_act(void) BANKED;
void publish_inv(void) BANKED;
void publish_treasures(void) BANKED;
void show_title(void) BANKED;
void start_battle(void) BANKED;
void retry_battle(void) BANKED;
void write_snapshot(void) BANKED;
extern CampDeploy snap_deploy;
#endif
