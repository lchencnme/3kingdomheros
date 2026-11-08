#pragma bank 5   /* game logic, HUD and map view: code bank 5 (design doc 11.3) */
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
#include "duel.h"
#include "../core/vm.h"
#include "../core/strategy.h"
#include "../core/item.h"
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
    G_ILIST, G_IAIM, G_XLIST, G_CAMP, G_SHOP
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

static Battle B;
static uint8_t state, sel, cur_x, cur_y, tent_x, tent_y, menu, ntarget, tsel, timer, next_state;
static uint8_t mitems[6], isel, xrecv, camp_sel, shop_sel, keep_inv, n_mitems, n_slist, ssel, aim_x, aim_y, aim_dirty, n_talk;
static uint8_t enemy_i, options, autosave_pending, pending_start, end_fired;   /* no initialised statics */
static uint8_t hud_dirty, last_hx, last_hy, hud_settle;
static uint16_t gold;
static const uint8_t blank16[16];

/* debug/test hooks (read by tests/emu/test_rom.py through the .noi symbol file) */

/* ---------------------------------------------------------------- helpers */
static void load(uint8_t i, ActiveUnit *u) { unit_load(i, u); }

static int8_t unit_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    units_snapshot(B.n_units);
    for (i = 0; i < B.n_units && i < MAX_SHOWN; i++)
        if (SNAP_ALIVE(i) && snap_x[i] == x && snap_y[i] == y) return (int8_t)i;
    return -1;
}

static uint8_t unit_name_tx(const ActiveUnit *u)
{
    if (u->character_id == 1) return TX_NAME_LIUBEI;
    if (u->character_id == 2) return TX_NAME_GUANYU;
    if (u->character_id == 3) return TX_NAME_ZHANGFEI;
    if (u->character_id == 8) return TX_NAME_JIANYONG;
    return class_name_tx[u->class_id];
}

/* units are composited into BG by mapview (design doc 12.2); OBJ only for the cursor and a "ghost"
 * of the selected unit at its tentative cell while the action is being chosen */
static void place_sprites(void)
{
    uint8_t i;
    for (i = 0; i < 2 * MAX_SHOWN; i++) move_sprite(i, 0, 0);
    if ((state == G_MENU || state == G_TARGET || state == G_SLIST || state == G_SAIM || state == G_ILIST
         || state == G_IAIM || state == G_XLIST) && sel < MAX_SHOWN) {
        int16_t sx = (int16_t)tent_x * 16 - (int16_t)cam_x + 8;
        int16_t sy = (int16_t)tent_y * 16 - (int16_t)cam_y + 16;
        uint8_t base = OBJT_PLAYER;
        set_sprite_tile(0, base); set_sprite_tile(1, (uint8_t)(base + 2));
        set_sprite_prop(0, 0); set_sprite_prop(1, 0);
        if (sx >= -8 && sx <= 168 && sy >= 0 && sy < (int16_t)VIEW_H + 16) {
            move_sprite(0, (uint8_t)sx, (uint8_t)sy);
            move_sprite(1, (uint8_t)(sx + 8), (uint8_t)sy);
        }
    }
    {
        uint8_t s = 2 * MAX_SHOWN;
        int16_t sx = (int16_t)cur_x * 16 - (int16_t)cam_x + 8;
        int16_t sy = (int16_t)cur_y * 16 - (int16_t)cam_y + 16;
        uint8_t hide = state == G_ENEMY || state == G_BANNER || state == G_OUTCOME || state == G_CAMP || state == G_SHOP;
        set_sprite_tile(s, OBJT_CURSOR); set_sprite_tile(s + 1, OBJT_CURSOR + 2);
        set_sprite_prop(s, 2); set_sprite_prop(s + 1, 2);
        move_sprite(s, (uint8_t)sx, hide ? 0 : (uint8_t)sy);
        move_sprite(s + 1, (uint8_t)(sx + 8), hide ? 0 : (uint8_t)sy);
    }
}

/* ---------------------------------------------------------------- HUD */
static void hud_unit_or_terrain(void)
{
    int8_t u = unit_at(cur_x, cur_y);
    hud_begin();
    if (u >= 0) {
        ActiveUnit a;
        load((uint8_t)u, &a);
        hud_tx(0, unit_name_tx(&a), 0); hud_space(0);
        if (a.character_id != ANON_CHARACTER) { hud_tx(0, class_name_tx[a.class_id], 0); hud_space(0); }
        hud_tx(0, TX_LV, 0); hud_num(0, a.level);
        hud_tx(1, TX_HP, 0); hud_space(1); hud_num(1, a.hp); hud_glyph(1, GLYPH_SLASH); hud_num(1, a.max_hp);
    } else {
        uint8_t t = grid_terrain(cur_x, cur_y) & 0x0F;
        hud_tx(0, TX_TERRAIN, 0); hud_space(0); hud_tx(0, terrain_name_tx[t], 0);
        hud_tx(1, TX_DR, 0); hud_space(1); hud_num(1, rules_terrain_dr(t)); hud_glyph(1, GLYPH_PERCENT);
    }
    hud_end();
}

static void build_menu(void)
{
    ActiveUnit a;
    uint8_t s;
    load(sel, &a);
    n_mitems = 0; n_slist = 0;
    for (s = 0; s < NUM_STRATEGIES; s++) if (strategy_known(&a, s)) SLIST[n_slist++] = s;
    if (ntarget) mitems[n_mitems++] = MI_ATTACK;
    if (n_slist && !(a.status_mask & (1u << ST_SILENCE))) mitems[n_mitems++] = MI_STRAT;
    {   /* 交谈: an adjacent unit with a TALK event this actor can trigger (design doc 4.3/9.1) */
        static const int8_t DX[4] = {0, -1, 1, 0}, DY[4] = {-1, 0, 0, 1};
        uint8_t k;
        n_talk = 0;
        for (k = 0; k < 4; k++) {
            int16_t x = (int16_t)tent_x + DX[k], y = (int16_t)tent_y + DY[k];
            int8_t u;
            VmFact f;
            if (x < 0 || y < 0 || x >= (int16_t)grid_w || y >= (int16_t)grid_h) continue;
            u = unit_at((uint8_t)x, (uint8_t)y);
            if (u < 0 || (uint8_t)u == sel) continue;
            f.trigger = TR_TALK; f.phase = B.phase; f.actor = sel; f.target = (uint8_t)u; f.unit = (uint8_t)u;
            if (vm_would_fire(&B, &f)) talks[n_talk++] = (uint8_t)u;
        }
        if (n_talk) mitems[n_mitems++] = MI_TALK;
        /* 道具 / 交换 (design doc 7.3): exchange = hand an item to an adjacent ally with a free slot */
        xrecv = 0xFF;
        if (inv_count(sel)) {
            mitems[n_mitems++] = MI_ITEM;
            for (k = 0; k < 4 && xrecv == 0xFF; k++) {
                int16_t x = (int16_t)tent_x + DX[k], y = (int16_t)tent_y + DY[k];
                int8_t u;
                ActiveUnit r;
                if (x < 0 || y < 0 || x >= (int16_t)grid_w || y >= (int16_t)grid_h) continue;
                u = unit_at((uint8_t)x, (uint8_t)y);
                if (u < 0 || (uint8_t)u == sel) continue;
                load((uint8_t)u, &r);
                if (r.faction == a.faction && inv_count((uint8_t)u) < INV_SLOTS) xrecv = (uint8_t)u;
            }
            if (xrecv != 0xFF) mitems[n_mitems++] = MI_EXCH;
        }
    }
    mitems[n_mitems++] = MI_WAIT;
    menu = ntarget ? 0 : (uint8_t)(n_mitems - 1);   /* default: attack if possible, else wait */
}

static void hud_menu(void)
{
    static const uint8_t label[6] = {TX_ACT_ATTACK, TX_ACT_STRAT, TX_ACT_WAIT, TX_ACT_TALK, TX_ACT_ITEM, TX_ACT_EXCH};
    uint8_t i;
    hud_begin();
    for (i = 0; i < n_mitems; i++) {
        uint8_t ln = (uint8_t)(n_mitems > 4 && i >= 3 ? 1 : 0);     /* up to 3 per line when 5-6 items */
        if (i == menu) hud_tx(ln, TX_ARROW, 0); else { hud_space(ln); hud_space(ln); }
        hud_tx(ln, label[mitems[i]], 0);
        hud_space(ln);
    }
    if (n_mitems <= 4) hud_tx(1, TX_MOVE_HINT, 0);
    hud_end();
    dbg_menu_item = mitems[menu];
}

static void hud_slist(void)
{
    ActiveUnit a;
    uint8_t s = SLIST[ssel];
    load(sel, &a);
    hud_begin();
    hud_tx(0, TX_ARROW, 0); hud_tx(0, strat_name_tx[s], 0); hud_space(0);
    hud_tx(0, TX_MP, 0); hud_num(0, strategy_defs[s].mp); hud_space(0);
    hud_tx(0, TX_RANGE, 0); hud_num(0, strategy_defs[s].range);
    if (a.mp < strategy_defs[s].mp) hud_tx(1, TX_SR_1, 0); else hud_tx(1, TX_CAST_HINT, 0);
    hud_end();
}

static void publish_inv(void)
{
    uint8_t s;
    for (s = 0; s < INV_SLOTS; s++) dbg_inv[s] = inv_get(0, s);
    dbg_gold = gold;
}

/* next non-empty slot of the selected unit after/before isel (dir +1 / -1); isel itself if alone */
static uint8_t next_item(uint8_t from, int8_t dir)
{
    uint8_t k, s = from;
    for (k = 0; k < INV_SLOTS; k++) {
        s = (uint8_t)((s + INV_SLOTS + dir) % INV_SLOTS);
        if (inv_get(sel, s) != ITEM_NONE) return s;
    }
    return from;
}

static void hud_ilist(uint8_t hint)
{
    uint8_t it = inv_get(sel, isel);
    hud_begin();
    hud_tx(0, TX_ARROW, 0);
    if (it != ITEM_NONE) hud_tx(0, item_name_tx[it], 0);
    hud_space(0); hud_num(0, inv_count(sel)); hud_glyph(0, GLYPH_SLASH); hud_num(0, INV_SLOTS);
    hud_tx(1, hint, 0);
    hud_end();
}

static uint8_t item_aim_check(void)
{
    ActiveUnit a;
    uint8_t ox, oy, r;
    load(sel, &a);
    ox = a.x; oy = a.y;
    if (ox != tent_x || oy != tent_y) { a.x = tent_x; a.y = tent_y; unit_store(sel, &a); }
    r = item_check(&B, sel, isel, aim_x, aim_y);
    if (ox != tent_x || oy != tent_y) { a.x = ox; a.y = oy; unit_store(sel, &a); }
    return r;
}

static void hud_item_aim(void)
{
    uint8_t r = item_aim_check(), it = inv_get(sel, isel);
    hud_begin();
    if (it != ITEM_NONE) hud_tx(0, item_name_tx[it], 0);
    hud_space(0); hud_tx(0, TX_ITEM_HINT, 0);
    if (r == IR_OK) hud_tx(1, TX_USE_OK, 0); else hud_tx(1, (uint8_t)(TX_IR_1 + r - 1u), 0);
    hud_end();
}

static void hud_item_result(const ItemResult *r)
{
    uint8_t k;
    hud_begin();
    if (r->item != ITEM_NONE) {
        k = item_defs[r->item].kind;
        hud_tx(0, item_name_tx[r->item], 0); hud_space(0);
        if (k == IK_HP) { hud_tx(0, TX_HEAL, 0); hud_num(0, r->amount); }
        else if (k == IK_MORALE) { hud_tx(0, TX_MORALE, 0); hud_glyph(0, GLYPH_PLUS); hud_num(0, r->amount); }
        else if (k == IK_MP) { hud_tx(0, TX_MP, 0); hud_glyph(0, GLYPH_PLUS); hud_num(0, r->amount); }
        else if (k == IK_FIRE) { hud_tx(0, TX_DMG, 0); hud_num(0, r->amount); }
        else if (k == IK_RAIN) hud_tx(0, TX_RAIN, 0);
        else if (k == IK_CLEAR) hud_tx(0, TX_CLEAR, 0);
        if (r->killed) { hud_tx(1, TX_RETREAT, 0); sfx_play(SFX_RETREAT); } else sfx_play(SFX_HIT);
    }
    hud_end();
    dbg_item_uses++;
    publish_inv();
}

static void hud_camp(void)
{
    hud_begin();
    hud_tx(0, TX_CAMP, 0); hud_space(0); hud_tx(0, TX_GOLD, 0); hud_num(0, gold);
    if (camp_sel == 0) hud_tx(1, TX_ARROW, 0); else { hud_space(1); hud_space(1); }
    hud_tx(1, TX_SHOP, 0); hud_space(1);
    if (camp_sel == 1) hud_tx(1, TX_ARROW, 0); else { hud_space(1); hud_space(1); }
    hud_tx(1, TX_DEPART, 0);
    hud_end();
    publish_inv();
}

static void hud_shop(uint8_t msg)
{
    hud_begin();
    hud_tx(0, TX_ARROW, 0); hud_tx(0, item_name_tx[shop_sel], 0); hud_space(0);
    hud_tx(0, TX_GOLD, 0); hud_num(0, item_defs[shop_sel].price);
    hud_tx(1, TX_GOLD, 0); hud_num(1, gold); hud_space(1);
    hud_tx(1, msg, 0);
    hud_end();
    publish_inv();
}

/* shop (design doc 7.3): check gold and a free slot first, then take the gold and give the item */
static void shop_buy(void)
{
    static const uint8_t buyers[4] = {0, 1, 2, 7};      /* 刘备 关羽 张飞 简雍: first with a free slot */
    uint16_t price = item_defs[shop_sel].price;
    uint8_t k, who = 0xFF;
    if (gold < price) { sfx_play(SFX_ERROR); hud_shop(TX_NO_GOLD); return; }
    for (k = 0; k < 4 && who == 0xFF; k++) if (inv_count(buyers[k]) < INV_SLOTS) who = buyers[k];
    if (who == 0xFF) { sfx_play(SFX_ERROR); hud_shop(TX_BAG_FULL); return; }
    gold = (uint16_t)(gold - price);
    inv_add(who, shop_sel);
    sfx_play(SFX_CONFIRM);
    hud_shop(TX_BOUGHT);
}

/* caster is assumed at its tentative cell: temporarily place it there for the check */
static uint8_t aim_check(void)
{
    ActiveUnit a;
    uint8_t ox, oy, r;
    load(sel, &a);
    ox = a.x; oy = a.y;
    if (ox != tent_x || oy != tent_y) { a.x = tent_x; a.y = tent_y; unit_store(sel, &a); }
    r = strategy_check(&B, sel, SLIST[ssel], aim_x, aim_y);
    if (ox != tent_x || oy != tent_y) { a.x = ox; a.y = oy; unit_store(sel, &a); }
    return r;
}

static void hud_aim(void)
{
    uint8_t r = aim_check();
    hud_begin();
    hud_tx(0, strat_name_tx[SLIST[ssel]], 0); hud_space(0); hud_tx(0, TX_CAST_HINT, 0);
    if (r == SR_OK) hud_tx(1, TX_CAST_OK, 0); else hud_tx(1, (uint8_t)(TX_SR_1 + r - 1u), 0);
    hud_end();
}

static void hud_strat_result(uint8_t sid, const StrategyResult *sr)
{
    uint8_t i, any_hit = 0, any_kill = 0;
    uint16_t total = 0;
    for (i = 0; i < sr->n; i++) { any_hit |= sr->hit[i]; any_kill |= sr->killed[i]; total += sr->amount[i]; }
    hud_begin();
    hud_tx(0, strat_name_tx[sid], 0); hud_space(0);
    if (!any_hit) hud_tx(0, TX_MISS, 0);
    else if (strategy_defs[sid].effect == FX_HEAL) { hud_tx(0, TX_HEAL, 0); hud_num(0, total); }
    else if (total) { hud_tx(0, TX_DMG, 0); hud_num(0, total); }
    else if (strategy_defs[sid].status != ST_NONE) hud_tx(0, TX_STATUS_ON, 0);
    if (any_kill) { hud_tx(1, TX_RETREAT, 0); sfx_play(SFX_RETREAT); }
    else if (any_hit) sfx_play(SFX_HIT);
    if (sr->levels) { hud_space(1); hud_tx(1, TX_LEVELUP, 0); }
    hud_end();
}

static void hud_target(void)
{
    ActiveUnit a, t;
    AttackPreview p;
    load(sel, &a);
    load(targets[tsel], &t);
    battle_preview(&a, tent_x, tent_y, &t, B.weather, &p);
    hud_begin();
    hud_tx(0, unit_name_tx(&t), 0); hud_space(0);
    hud_tx(0, TX_HP, 0); hud_num(0, t.hp);
    hud_tx(1, TX_HIT, 0); hud_num(1, p.hit_pct); hud_glyph(1, GLYPH_PERCENT); hud_space(1);
    hud_tx(1, TX_DMG, 0); hud_num(1, p.dmg);
    if (p.counter) { hud_space(1); hud_tx(1, TX_COUNTER, 0); hud_num(1, p.c_dmg); }
    hud_end();
}

static void hud_result(const AttackResult *r)
{
    if (r->killed || r->c_killed) sfx_play(SFX_RETREAT);
    else if (r->hit || r->c_hit) sfx_play(SFX_HIT);
    hud_begin();
    if (r->hit) { hud_tx(0, TX_DMG, 0); hud_num(0, r->dmg); } else hud_tx(0, TX_MISS, 0);
    if (r->killed) { hud_space(0); hud_tx(0, TX_RETREAT, 0); }
    if (r->levels) { hud_space(0); hud_tx(0, TX_LEVELUP, 0); }
    if (r->countered) {
        hud_tx(1, TX_COUNTER, 0); hud_space(1);
        if (r->c_hit) { hud_tx(1, TX_DMG, 0); hud_num(1, r->c_dmg); } else hud_tx(1, TX_MISS, 0);
        if (r->c_killed) { hud_space(1); hud_tx(1, TX_RETREAT, 0); }
    }
    hud_end();
}

static void hud_banner(uint8_t phase)
{
    hud_begin();
    hud_tx(0, phase == FAC_ENEMY ? TX_PHASE_ENEMY : TX_PHASE_PLAYER, B.round);
    hud_end();
}

/* ---------------------------------------------------------------- presentation (design doc 12.4/12.5) */
static void map_mode_restore(void)
{
    /* LCD is off (fs_present returns that way): reload map-mode VRAM, palettes, HUD */
    set_bkg_palette(0, 8, tech01_bg_pals);
    VBK_REG = VBK_TILES;
    rom_set_bkg_data(TECH01_BANK, 0, TECH01_BG_TILES, tech01_bg_tiles);
    set_bkg_data(TECH01_BG_TILES, 1, blank16);
    hud_init(TECH01_BG_TILES);
    mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
    SHOW_WIN; SHOW_SPRITES;
    DISPLAY_ON;
}

static uint8_t count_alive(void)
{
    uint8_t i, n = 0;
    units_snapshot(B.n_units);
    for (i = 0; i < B.n_units && i < MAX_SHOWN; i++) if (SNAP_ALIVE(i)) n++;
    return n;
}

/* play the VM output queue in order: runs of text pages, and the duel presentation */
static void play_queue(void)
{
    uint8_t i = 0;
    while (i < DQ->n) {
        if (DQ->speaker[i] == DQ_DUEL) {
            DuelInfo *d = DUEL_INFO;
            FsSide sw, sl;
            AttackResult r;
            ActiveUnit u;
            load(d->winner, &u); sw.name_tx = unit_name_tx(&u);
            load(d->loser, &u); sl.name_tx = unit_name_tx(&u);
            sw.cls = d->w_cls; sw.fac = d->w_fac; sw.hp = d->w_hp;
            sl.cls = d->l_cls; sl.fac = d->l_fac; sl.hp = d->l_hp;
            memset(&r, 0, sizeof r);
            r.hit = 1; r.dmg = (uint16_t)(d->l_hp - u.hp); r.killed = d->retreated;
            fs_present(&sw, &sl, &r);
            i++;
        } else {
            i = dialog_run_from(i);
        }
    }
    DQ->n = 0;
}

/* run the event VM for one fact; play queued dialogue (design doc 9) */
static void fire(uint8_t trig, uint8_t phase, uint8_t actor, uint8_t target, uint8_t unit)
{
    VmFact f;
    uint8_t r;
    uint8_t n_before = B.n_units, alive_before = count_alive();
    f.trigger = trig; f.phase = phase; f.actor = actor; f.target = target; f.unit = unit;
    r = vm_fire(&B, &f);
    if (r != VM_OK) { dbg_vm_err = r; B.outcome = OUT_LOSE; }   /* BAD_SCRIPT: safe stop (9.3) */
    if (vm_units_changed || B.n_units != n_before || count_alive() != alive_before) {
        uint8_t o = SVBK_REG;                   /* spawns/retreats change the approach-field sources */
        SVBK_REG = WB_SCRATCH; ai_invalidate(W7_AI); SVBK_REG = o;
    }
    if (DQ->n) { play_queue(); map_mode_restore(); }
}

static void side_of_unit(uint8_t i, FsSide *s)
{
    ActiveUnit u;
    load(i, &u);
    s->cls = u.class_id; s->fac = u.faction; s->hp = u.hp; s->name_tx = unit_name_tx(&u);
}

/* resolve (sealed) then present the attack fullscreen, then restore the map */
static void attack_and_present(uint8_t ai, uint8_t di, AttackResult *r)
{
    FsSide sa, sd;
    memset(r, 0, sizeof *r);
    fire(TR_PRE_ACTION, B.phase, ai, di, di);   /* 4.4: PRE_ACTION may replace the attack (DUEL) */
    if (vm_action_replaced || B.outcome) {
        if (r) { ActiveUnit t_; load(di, &t_); if (!unit_alive(&t_)) fire(TR_UNIT_RETREAT, B.phase, ai, di, di); }
        return;
    }
    side_of_unit(ai, &sa);
    side_of_unit(di, &sd);
    battle_attack(&B, ai, di, r);           /* logic sealed before any presentation (design doc 4.4) */
    fs_present(&sa, &sd, r);
    map_mode_restore();
    if (r->killed) fire(TR_UNIT_RETREAT, B.phase, ai, di, di);
    if (r->c_killed) fire(TR_UNIT_RETREAT, B.phase, di, ai, ai);
}

/* ---------------------------------------------------------------- save (design doc 11.6) */
static uint8_t do_suspend_save(void)
{
    SaveUi ui;
    uint8_t r;
    ui.battle_id = 0xFFFEu; ui.cam_x = cam_x; ui.cam_y = cam_y; ui.cur_x = cur_x; ui.cur_y = cur_y;
    ui.presentation = 0; ui.text_speed = 0; ui.options = options; ui.gold = gold;
    r = save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
    dbg_save_result = r; dbg_saves++;
    return r;
}

static uint8_t do_suspend_load(void)
{
    SaveUi ui;
    uint8_t r;
    units_clear();
    wram_init_grid_rom(TECH01_W, TECH01_H, TECH01_BANK, tech01_terrain);
    r = load_battle(SAVE_SLOT_SUSPEND, &B, &ui);
    if (r != SAVE_OK) return r;
    pending_start = 0; end_fired = 0; DQ->n = 0;
    B.turn_limit = 30;                          /* from the battle definition, not the save */
    options = ui.options;
    gold = (uint16_t)ui.gold;
    cam_x = ui.cam_x; cam_y = ui.cam_y; cur_x = ui.cur_x; cur_y = ui.cur_y;
    {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH; ai_invalidate(W7_AI); SVBK_REG = o;
    }
    return SAVE_OK;
}

/* ---------------------------------------------------------------- flow */
static void start_battle(void)
{
    uint8_t i;
    memset(&B, 0, sizeof B);
    rng_seed(&B.rng, 1);
    B.round = 1; B.turn_limit = 30; B.leader = 0;
    units_clear();
    wram_init_grid_rom(TECH01_W, TECH01_H, TECH01_BANK, tech01_terrain);
    vm_clear_battle_flags();                   /* once bits + battle-scope flags are per battle */
    DQ->n = 0;
    end_fired = 0;
    {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH;
        ai_invalidate(W7_AI);
        SVBK_REG = o;
    }
    for (i = 0; i < TECH01_UNITS; i++) {
        const Tech01Unit *t = &tech01_units[i];
        battle_spawn(&B, i, t->faction == 0 ? (uint16_t)(i + 1) : ANON_CHARACTER, t->cls, t->faction, 1, t->x, t->y);
    }
    /* inventory (7.1): a new game starts from the battle's item list; departing from the camp keeps what
     * the player's officers carry (shop purchases) and only re-issues the enemies' items */
    if (!keep_inv) inv_clear_all();
    else {
        uint8_t s;
        for (i = 0; i < MAX_UNITS; i++)
            if (i >= TECH01_UNITS || tech01_units[i].faction != FAC_PLAYER)
                for (s = 0; s < INV_SLOTS; s++) if (inv_get(i, s) != ITEM_NONE) inv_set(i, s, ITEM_NONE);
    }
    for (i = 0; i < TECH01_START_ITEMS; i++)
        if (!keep_inv || tech01_units[tech01_start_items[i][0]].faction != FAC_PLAYER)
            inv_add(tech01_start_items[i][0], tech01_start_items[i][1]);
    if (!keep_inv) gold = 0;
    keep_inv = 0;
    publish_inv();
    cur_x = tech01_units[0].x; cur_y = tech01_units[0].y;
    battle_phase_start(&B, FAC_PLAYER);
    state = G_BANNER; timer = 45; next_state = G_IDLE;
    hud_banner(FAC_PLAYER);
    autosave_pending = (uint8_t)(options & OPT_AUTOSAVE);
    pending_start = 1;                         /* BATTLE_START + PHASE_START fire once the LCD is on */
}

static uint8_t player_can_act(void)
{
    uint8_t i;
    ActiveUnit u;
    for (i = 0; i < B.n_units; i++) {
        load(i, &u);
        if (u.faction == FAC_PLAYER && battle_can_act(&u)) return 1;
    }
    return 0;
}

static void begin_enemy_phase(void)
{
    uint8_t o = SVBK_REG;
    battle_phase_end(&B);                      /* PLAYER end */
    if (B.outcome) return;
    battle_phase_start(&B, FAC_ALLY);          /* no allies in TECH01: start/end still run */
    fire(TR_PHASE_START, FAC_ALLY, 0xFF, 0xFF, 0xFF);
    if (B.outcome) return;
    battle_phase_end(&B);
    if (B.outcome) return;
    battle_phase_start(&B, FAC_ENEMY);
    fire(TR_PHASE_START, FAC_ENEMY, 0xFF, 0xFF, 0xFF);
    if (B.outcome) return;
    SVBK_REG = WB_SCRATCH;
    ai_invalidate(W7_AI);
    ai_field_request(&B, W7_AI, MT_FOOT, SIDE_E);     /* pre-warm the most common move type */
    SVBK_REG = o;
    enemy_i = 0;
    state = G_BANNER; timer = 45; next_state = G_ENEMY;
    hud_banner(FAC_ENEMY);
}

static void after_action(void)
{
    if (B.outcome) return;
    if (!player_can_act()) begin_enemy_phase();
}

static void collect_targets(void)
{
    uint8_t i;
    ActiveUnit a, t;
    load(sel, &a);
    ntarget = 0;
    for (i = 0; i < B.n_units && ntarget < 8; i++) {
        load(i, &t);
        if (!unit_alive(&t) || !factions_hostile(a.faction, t.faction)) continue;
        if (battle_in_range(&a, tent_x, tent_y, &t)) targets[ntarget++] = i;
    }
}

static void do_preview(uint8_t u)
{
    ActiveUnit a;
    uint8_t o = SVBK_REG;
    uint16_t t0 = sys_time;
    load(u, &a);
    SVBK_REG = WB_SCRATCH;
    path_move_range(W7_MR, a.x, a.y, (a.status_mask & (1u << ST_ROOT)) ? 0 : a.move,
                    rules_class_move_type(a.class_id), faction_side(a.faction));
    SVBK_REG = o;
    dbg_t_range = sys_time - t0;
    sel = u;
    state = G_MOVE;
    mapview_highlight(1);
    hud_begin(); hud_tx(0, TX_MOVE_HINT, 0); hud_end();
}

/* 4.3 confirm boundary: commit the move, run POST_MOVE; returns 0 if the action must not happen */
static uint8_t commit_move_if_needed(void)
{
    ActiveUnit a;
    load(sel, &a);
    if (a.x != tent_x || a.y != tent_y) {
        battle_move(sel, tent_x, tent_y);
        fire(TR_POST_MOVE, FAC_PLAYER, sel, 0xFF, 0xFF);
        load(sel, &a);
        if (B.outcome || !unit_alive(&a)) return 0;
    }
    return 1;
}

/* item / exchange commits live in their own functions so their locals stay out of logic_player's frame
 * (the 512 B stack, design doc 11.4) */
static void do_item_use(void)
{
    ItemResult ir;
    if (commit_move_if_needed()) {
        item_use(&B, sel, isel, aim_x, aim_y, &ir);
        hud_item_result(&ir);
        if (ir.killed) fire(TR_UNIT_RETREAT, B.phase, sel, ir.target, ir.target);
        cur_x = tent_x; cur_y = tent_y;
        state = G_SHOW; timer = 50; next_state = G_IDLE;
    } else { cur_x = tent_x; cur_y = tent_y; state = G_IDLE; hud_dirty = 1; after_action(); }
}

static void do_exchange(void)
{
    ActiveUnit r;
    uint8_t s, it = inv_get(sel, isel);
    if (commit_move_if_needed()) {
        for (s = 0; s < INV_SLOTS && inv_get(xrecv, s) != ITEM_NONE; s++) {}
        if (s < INV_SLOTS && item_exchange(sel, isel, xrecv, s) == IR_OK) {
            load(xrecv, &r);
            hud_begin(); hud_tx(0, TX_GIVE, 0); hud_tx(0, unit_name_tx(&r), 0); hud_space(0);
            hud_tx(0, item_name_tx[it], 0); hud_end();
            sfx_play(SFX_CONFIRM);
            publish_inv();
        } else { load(sel, &r); r.action_flags |= AF_ACTED; unit_store(sel, &r); }
        cur_x = tent_x; cur_y = tent_y;
        state = G_SHOW; timer = 40; next_state = G_IDLE;
    } else { cur_x = tent_x; cur_y = tent_y; state = G_IDLE; hud_dirty = 1; after_action(); }
}

static void logic_player(void)
{
    int8_t u;
    if ((state == G_IDLE || state == G_MOVE) && (in_dx || in_dy)) {
        int16_t nx = (int16_t)cur_x + in_dx, ny = (int16_t)cur_y + in_dy;
        if (nx >= 0 && ny >= 0 && nx < (int16_t)grid_w && ny < (int16_t)grid_h) { cur_x = (uint8_t)nx; cur_y = (uint8_t)ny; }
    }
    switch (state) {
    case G_IDLE:
        if (in_pressed & J_A) {
            ActiveUnit a;
            u = unit_at(cur_x, cur_y);
            if (u >= 0) {
                load((uint8_t)u, &a);
                if (a.faction == FAC_PLAYER && battle_can_act(&a)) { sfx_play(SFX_CONFIRM); do_preview((uint8_t)u); }
            }
        } else if (in_pressed & J_START) {
            begin_enemy_phase();               /* end turn */
        } else if (in_pressed & J_SELECT) {
            uint8_t r = do_suspend_save();     /* only in the stable player idle state (11.6) */
            hud_begin(); hud_tx(0, r == SAVE_OK ? TX_SAVED : TX_SAVE_FAIL, 0); hud_end();
            hud_settle = 90; hud_dirty = 1;    /* keep the message up for a moment */
        }
        break;
    case G_MOVE:
        if (in_pressed & J_B) {
            ActiveUnit a;
            load(sel, &a);
            cur_x = a.x; cur_y = a.y;
            state = G_IDLE; mapview_highlight(0); hud_dirty = 1;
        } else if (in_pressed & J_A) {
            uint8_t ok, o = SVBK_REG;
            SVBK_REG = WB_SCRATCH;
            ok = path_endpoint_ok(W7_MR, cur_x, cur_y);
            SVBK_REG = o;
            if (!ok) sfx_play(SFX_ERROR);
            if (ok) {
                sfx_play(SFX_CONFIRM);
                tent_x = cur_x; tent_y = cur_y;
                collect_targets();
                build_menu();
                state = G_MENU;
                mapview_highlight(0);
                hud_menu();
            }
        }
        break;
    case G_MENU:
        if (n_mitems > 1 && (in_dx > 0 || in_dy > 0)) { menu = (uint8_t)((menu + 1) % n_mitems); hud_menu(); }
        else if (n_mitems > 1 && (in_dx < 0 || in_dy < 0)) { menu = (uint8_t)((menu + n_mitems - 1) % n_mitems); hud_menu(); }
        else if (in_pressed & J_B) { state = G_MOVE; mapview_highlight(1); hud_begin(); hud_tx(0, TX_MOVE_HINT, 0); hud_end(); }
        else if (in_pressed & J_A) {
            uint8_t it = mitems[menu];
            if (it == MI_ATTACK) { tsel = 0; state = G_TARGET; cur_x = tent_x; cur_y = tent_y; hud_target(); }
            else if (it == MI_STRAT) { ssel = 0; state = G_SLIST; hud_slist(); }
            else if (it == MI_ITEM) { isel = next_item(INV_SLOTS - 1, 1); state = G_ILIST; hud_ilist(TX_ITEM_HINT); }
            else if (it == MI_EXCH) { isel = next_item(INV_SLOTS - 1, 1); state = G_XLIST; hud_ilist(TX_GIVE_HINT); }
            else if (it == MI_TALK) {
                ActiveUnit a;
                uint8_t ok = commit_move_if_needed();
                if (ok) fire(TR_TALK, B.phase, sel, talks[0], talks[0]);
                load(sel, &a);
                if (unit_alive(&a)) { a.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED); unit_store(sel, &a); }
                cur_x = tent_x; cur_y = tent_y;
                state = G_IDLE; hud_dirty = 1;
                after_action();
            }
            else {
                ActiveUnit a;
                commit_move_if_needed();
                load(sel, &a);
                if (unit_alive(&a)) { a.action_flags |= AF_ACTED; unit_store(sel, &a); }
                cur_x = tent_x; cur_y = tent_y;
                state = G_IDLE; hud_dirty = 1;
                after_action();
            }
        }
        break;
    case G_SLIST:
        if (in_dx > 0 || in_dy > 0) { ssel = (uint8_t)((ssel + 1) % n_slist); hud_slist(); }
        else if (in_dx < 0 || in_dy < 0) { ssel = (uint8_t)((ssel + n_slist - 1) % n_slist); hud_slist(); }
        else if (in_pressed & J_B) { state = G_MENU; hud_menu(); }
        else if (in_pressed & J_A) {
            ActiveUnit a;
            load(sel, &a);
            if (a.mp < strategy_defs[SLIST[ssel]].mp) sfx_play(SFX_ERROR);
            else { aim_x = tent_x; aim_y = tent_y; state = G_SAIM; aim_dirty = 1; sfx_play(SFX_CONFIRM); }
        }
        break;
    case G_SAIM:
        if (in_dx || in_dy) {
            int16_t nx = (int16_t)aim_x + in_dx, ny = (int16_t)aim_y + in_dy;
            if (nx >= 0 && ny >= 0 && nx < (int16_t)grid_w && ny < (int16_t)grid_h) { aim_x = (uint8_t)nx; aim_y = (uint8_t)ny; aim_dirty = 1; }
        } else if (in_pressed & J_B) { state = G_SLIST; cur_x = tent_x; cur_y = tent_y; hud_slist(); }
        else if (in_pressed & J_A) {
            if (aim_check() != SR_OK) sfx_play(SFX_ERROR);
            else if (commit_move_if_needed()) {    /* 4.3 confirm boundary */
                StrategyResult sr;
                uint8_t sid = SLIST[ssel], k;
                strategy_cast(&B, sel, sid, aim_x, aim_y, &sr);
                dbg_strat_casts++;
                hud_strat_result(sid, &sr);
                for (k = 0; k < sr.n; k++) if (sr.killed[k]) fire(TR_UNIT_RETREAT, B.phase, sel, sr.unit[k], sr.unit[k]);
                cur_x = tent_x; cur_y = tent_y;
                state = G_SHOW; timer = 50; next_state = G_IDLE;
            } else { cur_x = tent_x; cur_y = tent_y; state = G_IDLE; hud_dirty = 1; after_action(); }
        }
        if (state == G_SAIM) { cur_x = aim_x; cur_y = aim_y; if (aim_dirty) { hud_aim(); aim_dirty = 0; } }
        break;
    case G_ILIST:
    case G_XLIST:
        if (in_dx > 0 || in_dy > 0) { isel = next_item(isel, 1); hud_ilist(state == G_ILIST ? TX_ITEM_HINT : TX_GIVE_HINT); }
        else if (in_dx < 0 || in_dy < 0) { isel = next_item(isel, -1); hud_ilist(state == G_ILIST ? TX_ITEM_HINT : TX_GIVE_HINT); }
        else if (in_pressed & J_B) { state = G_MENU; hud_menu(); }
        else if ((in_pressed & J_A) && state == G_ILIST) {
            aim_x = tent_x; aim_y = tent_y; state = G_IAIM; aim_dirty = 1; sfx_play(SFX_CONFIRM);
        } else if (in_pressed & J_A) do_exchange();   /* 交换: hand the item to the adjacent ally */
        break;
    case G_IAIM:
        if (in_dx || in_dy) {
            int16_t nx = (int16_t)aim_x + in_dx, ny = (int16_t)aim_y + in_dy;
            if (nx >= 0 && ny >= 0 && nx < (int16_t)grid_w && ny < (int16_t)grid_h) { aim_x = (uint8_t)nx; aim_y = (uint8_t)ny; aim_dirty = 1; }
        } else if (in_pressed & J_B) { state = G_ILIST; cur_x = tent_x; cur_y = tent_y; hud_ilist(TX_ITEM_HINT); }
        else if (in_pressed & J_A) {
            if (item_aim_check() != IR_OK) sfx_play(SFX_ERROR);
            else do_item_use();
        }
        if (state == G_IAIM) { cur_x = aim_x; cur_y = aim_y; if (aim_dirty) { hud_item_aim(); aim_dirty = 0; } }
        break;
    case G_TARGET:
        if (in_dx > 0 || in_dy > 0) { tsel = (uint8_t)((tsel + 1) % ntarget); hud_target(); }
        else if (in_dx < 0 || in_dy < 0) { tsel = (uint8_t)((tsel + ntarget - 1) % ntarget); hud_target(); }
        else if (in_pressed & J_B) { state = G_MENU; hud_menu(); }
        else if (in_pressed & J_A) {
            AttackResult r;
            if (commit_move_if_needed())       /* 4.3 confirm boundary */
                attack_and_present(sel, targets[tsel], &r);
            else { r.hit = 0; r.killed = 0; r.countered = 0; r.levels = 0; r.dmg = 0; }
            cur_x = tent_x; cur_y = tent_y;
            hud_result(&r);
            state = G_SHOW; timer = 50; next_state = G_IDLE;
        }
        break;
    }
    if (state == G_TARGET) {
        ActiveUnit t;
        load(targets[tsel], &t);
        cur_x = t.x; cur_y = t.y;
    }
}

static void mark_acted(uint8_t i)
{
    ActiveUnit u;
    load(i, &u); u.action_flags |= AF_ACTED; unit_store(i, &u);
}

/* AI strategy / item command (design doc 8.3); separate frame for the 512 B stack */
static void enemy_special(const AiCommand *c)
{
    static union { StrategyResult sr; ItemResult ir; } r;      /* static: keeps 28 B off the stack */
    uint8_t k, ok;
    if (c->action == ACT_STRATEGY) {
        ok = strategy_cast(&B, enemy_i, c->param, c->tx, c->ty, &r.sr) == SR_OK;
        if (ok) {
            dbg_ai_strats++;
            hud_strat_result(c->param, &r.sr);
            for (k = 0; k < r.sr.n; k++) if (r.sr.killed[k]) fire(TR_UNIT_RETREAT, B.phase, enemy_i, r.sr.unit[k], r.sr.unit[k]);
        }
    } else {
        ok = item_use(&B, enemy_i, c->param, c->tx, c->ty, &r.ir) == IR_OK;
        if (ok) { dbg_ai_items++; hud_item_result(&r.ir); }
    }
    if (!ok) { mark_acted(enemy_i); hud_begin(); hud_end(); }   /* never expected: AI proposes legal commands */
}

/* small helpers keep enemy_step's frame tiny: it sits under ai_decide, the deepest call chain */
static void enemy_skip_idle(void)
{
    ActiveUnit u;
    while (enemy_i < B.n_units) {
        load(enemy_i, &u);
        if (u.faction == FAC_ENEMY && battle_can_act(&u)) break;
        enemy_i++;
    }
}

static uint8_t unit_pos(uint8_t i, uint8_t *x, uint8_t *y)
{
    ActiveUnit u;
    load(i, &u);
    *x = u.x; *y = u.y;
    return unit_alive(&u);
}

static void enemy_attack(uint8_t target)
{
    AttackResult r;
    attack_and_present(enemy_i, target, &r);
    hud_result(&r);
}

/* design doc 8.4: one cached approach field. When the next enemy uses another move type, start its field
 * now as a cooperative job so it is built while this action's result is shown, not inside the decision */
static void prewarm_next(void)
{
    ActiveUnit u;
    uint8_t j, m, o;
    for (j = (uint8_t)(enemy_i + 1u); j < B.n_units; j++) {
        load(j, &u);
        if (u.faction == FAC_ENEMY && battle_can_act(&u)) break;
    }
    if (j >= B.n_units) return;
    m = rules_class_move_type(u.class_id);
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    if (W7_AI->field_state == 0 || W7_AI->field_mtype != m || W7_AI->field_side != SIDE_E)
        ai_field_request(&B, W7_AI, m, SIDE_E);
    SVBK_REG = o;
}

static void enemy_step(void)
{
    AiCommand c;
    uint8_t o, ux, uy;
    uint16_t t0;
    enemy_skip_idle();
    if (enemy_i >= B.n_units) {
        fire(TR_ROUND_END, FAC_ENEMY, 0xFF, 0xFF, 0xFF);   /* round number of the round that ends */
        if (B.outcome) return;
        battle_phase_end(&B);                  /* ENEMY end: TTL, round-limit check, round++ */
        if (B.outcome) return;
        battle_phase_start(&B, FAC_PLAYER);
        fire(TR_PHASE_START, FAC_PLAYER, 0xFF, 0xFF, 0xFF);
        if (B.outcome) return;
        state = G_BANNER; timer = 45; next_state = G_IDLE;
        hud_banner(FAC_PLAYER);
        autosave_pending = (uint8_t)(options & OPT_AUTOSAVE);
        return;
    }
    unit_pos(enemy_i, &ux, &uy);
    o = SVBK_REG;
    t0 = sys_time;
    SVBK_REG = WB_SCRATCH;
    ai_decide(&B, W7_AI, W7_MR, enemy_i, AI_AGGRESSIVE, &c);
    SVBK_REG = o;
    dbg_t_ai = sys_time - t0;
    if (c.x != ux || c.y != uy) {
        battle_move(enemy_i, c.x, c.y);
        fire(TR_POST_MOVE, FAC_ENEMY, enemy_i, 0xFF, 0xFF);
        if (B.outcome || !unit_pos(enemy_i, &ux, &uy)) c.action = ACT_WAIT;   /* 4.3: the move's event ended the action */
    }
    cur_x = c.x; cur_y = c.y;                  /* camera follows the actor */
    if (c.action == ACT_ATTACK) {
        enemy_attack(c.target);
        timer = 50;
    } else if (c.action == ACT_STRATEGY || c.action == ACT_ITEM) {
        enemy_special(&c);
        if (c.action == ACT_STRATEGY) { cur_x = c.tx; cur_y = c.ty; }
        timer = 50;
    } else {
        mark_acted(enemy_i);
        timer = 20;
        hud_begin(); hud_end();
    }
    if (!B.outcome) prewarm_next();
    enemy_i++;
    state = G_SHOW; next_state = G_ENEMY;
}

static void logic(void)
{
    if (pending_start && state == G_BANNER) {
        pending_start = 0;
        fire(TR_BATTLE_START, FAC_PLAYER, 0xFF, 0xFF, 0xFF);
        fire(TR_PHASE_START, FAC_PLAYER, 0xFF, 0xFF, 0xFF);
    }
    if (B.outcome && !end_fired && state != G_TITLE) {
        end_fired = 1;
        fire(TR_BATTLE_END, B.phase, 0xFF, 0xFF, 0xFF);
    }
    if (B.outcome && state != G_OUTCOME && state != G_SHOW && state != G_TITLE && state != G_CAMP && state != G_SHOP) {
        state = G_OUTCOME;
        save_invalidate(SAVE_SLOT_SUSPEND);     /* never "continue" a finished battle */
        if (B.outcome == OUT_WIN) gold = (uint16_t)(gold + WIN_GOLD);
        hud_begin(); hud_tx(0, B.outcome == OUT_WIN ? TX_WIN_CAMP : TX_LOSE, 0); hud_end();
        publish_inv();
        return;
    }
    if (autosave_pending && state == G_BANNER) {
        /* design doc 11.6: autosave at player phase start, performed while the phase banner is shown
         * (the player is waiting anyway); the banner time already spent is deducted. ~0.6 s blocking. */
        uint16_t t0 = sys_time, el;
        autosave_pending = 0;
        do_suspend_save();
        el = sys_time - t0;
        timer = el >= 35u ? 10u : (uint8_t)(45u - el);
    }
    switch (state) {
    case G_SHOW:
    case G_BANNER:
        if (timer) timer--;
        {   /* cooperative AI field job (design doc 4.2/8.4): runs while banners/results are shown */
            uint8_t ready, o = SVBK_REG;
            SVBK_REG = WB_SCRATCH;
            ready = ai_field_pump(W7_AI, 12);
            SVBK_REG = o;
            if (state == G_BANNER && next_state == G_ENEMY && !ready) break;   /* "敌军思考中" */
        }
        if (!timer || (in_pressed & (J_A | J_B))) {
            state = next_state;
            if (state == G_IDLE) { hud_dirty = 1; after_action(); }
        }
        break;
    case G_ENEMY:
        enemy_step();
        break;
    case G_OUTCOME:
        if ((in_pressed & J_A) && B.outcome == OUT_WIN) { state = G_CAMP; camp_sel = 0; hud_camp(); }
        else if (in_pressed & J_A) {
            DISPLAY_OFF;
            start_battle(); mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
            DISPLAY_ON;
        }
        break;
    case G_CAMP:                               /* camp node (design doc 2.2): shop / depart */
        if (in_dx || in_dy) { camp_sel ^= 1u; hud_camp(); }
        else if ((in_pressed & J_A) && camp_sel == 0) { state = G_SHOP; shop_sel = 0; hud_shop(TX_SHOP_HINT); }
        else if (in_pressed & J_A) {
            DISPLAY_OFF;
            keep_inv = 1;
            start_battle(); mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
            DISPLAY_ON;
        }
        break;
    case G_SHOP:
        if (in_dx > 0 || in_dy > 0) { shop_sel = (uint8_t)((shop_sel + 1) % NUM_ITEMS); hud_shop(TX_SHOP_HINT); }
        else if (in_dx < 0 || in_dy < 0) { shop_sel = (uint8_t)((shop_sel + NUM_ITEMS - 1) % NUM_ITEMS); hud_shop(TX_SHOP_HINT); }
        else if (in_pressed & J_B) { state = G_CAMP; hud_camp(); }
        else if (in_pressed & J_A) shop_buy();
        break;
    case G_TITLE:
        if (in_pressed & J_START) {
            if (do_suspend_load() == SAVE_OK) {
                state = G_IDLE; hud_dirty = 1;
                DISPLAY_OFF; mapview_init(&tech01_meta[0][0], tech01_terrain_pal); DISPLAY_ON;
            } else { hud_begin(); hud_tx(0, TX_LOAD_FAIL, 0); hud_end(); start_battle(); }
        } else if (in_pressed & J_A) {
            start_battle();
        }
        break;
    default:
        logic_player();
    }
    /* cursor info HUD: refresh once the cursor has rested 8 frames (longer than the 4-frame key repeat,
     * so holding a direction never re-rasterises the HUD) */
    if (state == G_IDLE) {
        if (cur_x != last_hx || cur_y != last_hy) { last_hx = cur_x; last_hy = cur_y; hud_settle = 8; hud_dirty = 1; }
        else if (hud_settle) hud_settle--;
        if (hud_dirty && !hud_settle) { hud_unit_or_terrain(); hud_dirty = 0; }
    }
}

static void publish_debug(void)
{
    dbg_state = state; dbg_sel = sel; dbg_cur_x = cur_x; dbg_cur_y = cur_y;
    dbg_cam_x = cam_x; dbg_cam_y = cam_y;
    dbg_outcome = B.outcome; dbg_round = B.round; dbg_phase = B.phase;
    dbg_rng = B.rng.state; dbg_draws = B.rng.draws;
    dbg_n_units = B.n_units;                      /* tests read the 40 B unit records in WRAM bank1 */
    dbg_frame_lo++;
}

void game_run(void) BANKED
{
    uint8_t i;
    DISPLAY_OFF;
    if (_cpu == CGB_TYPE) cpu_fast();          /* design doc 11.2: double speed */
    {   /* stack canary + high-water pattern (11.4): fill CE00 .. current SP-32 with A5 */
        uint8_t probe;
        uint16_t top = (uint16_t)&probe - 32u;
        memset((void *)0xCE00, 0xA5, (uint16_t)(top - 0xCE00u));
    }
    options = OPT_AUTOSAVE;
    {   /* CRC32 self-test of the SM83 inner loop: "123456789" -> CBF43926 (ISO-HDLC check value) */
        static const uint8_t chk[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        uint8_t buf[9];
        uint8_t o = SVBK_REG;
        memcpy(buf, chk, 9);
        { uint8_t cp; uint32_t sq; save_probe(SAVE_SLOT_NORMAL1, &cp, &sq); }   /* loads the CRC tables */
        dbg_crc_check = crc32_update(0xFFFFFFFFul, buf, 9) ^ 0xFFFFFFFFul;
        SVBK_REG = o;
    }

    set_bkg_palette(0, 8, tech01_bg_pals);
    set_sprite_palette(0, 4, tech01_obj_pals);
    VBK_REG = VBK_TILES;
    rom_set_bkg_data(TECH01_BANK, 0, TECH01_BG_TILES, tech01_bg_tiles);
    set_bkg_data(TECH01_BG_TILES, 1, blank16);
    rom_set_sprite_data(TECH01_BANK, 0, TECH01_OBJ_TILES, tech01_obj_tiles);
    for (i = 0; i < 20; i++) {
        set_win_tile_xy(i, 0, TECH01_BG_TILES); set_win_tile_xy(i, 1, TECH01_BG_TILES);
        set_win_tile_xy(i, 2, TECH01_BG_TILES); set_win_tile_xy(i, 3, TECH01_BG_TILES);
    }
    WY_REG = 112; WX_REG = 7;
    hud_init(TECH01_BG_TILES);

    start_battle();
    cam_x = 0; cam_y = (uint16_t)(cur_y * 16u + 8u - 56u);
    mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
    {
        uint8_t copy;
        uint32_t seq;
        if (save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK) {
            state = G_TITLE;
            autosave_pending = 0;               /* never overwrite the found save before the choice */
            hud_begin(); hud_tx(0, TX_CONTINUE_Q, 0); hud_tx(1, TX_CONTINUE_KEYS, 0); hud_end();
        }
    }

    input_init();
    audio_init();
    music_play(&song_battle);
    SPRITES_8x16;
    SHOW_BKG; SHOW_WIN; SHOW_SPRITES;
    DISPLAY_ON;

    for (;;) {
        input_update();
        logic();
        mapview_camera_follow(cur_x, cur_y);
        mapview_prepare();
        hud_service();
        place_sprites();
        publish_debug();
        wait_vbl_done();
        hud_commit();          /* GDMA first: short and atomic */
        mapview_commit();
    }
}
