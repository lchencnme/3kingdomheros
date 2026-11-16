#pragma bank 10   /* same bank as vblank.c: vb_wait/vb_gdma_wait are plain calls (design doc 11.3) */
/* Camp node (design doc 2.2, 2.3, 7.1, 7.3): its own full screen (paper + text lines), blocking like the
 * dialogue mode. Commands: 军议 (council, required before departing) / 装备 / 交换 / 商店 (buy + sell) /
 * 出阵 (deployment: choose officers and their order; forced officers stay in).
 *
 * Screen: 9 text lines of 16 px (rows 2k, 2k+1; columns 1..18), 36 tiles each: lines 0-2 bank1 ids 0..107,
 * lines 3-5 bank1 ids 128..235, lines 6-8 bank0 ids 1..108. Palette 7 = paper/ink. Window and sprites off. */
#include "../core/vm.h"
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "camp.h"
#include "hud.h"
#include "dialog.h"
#include "dialogq.h"
#include "vblank.h"
#include "audio.h"
#include "input.h"
#include "debug.h"
#include "../core/item.h"
#include "../gen/text_data.h"
#include "../gen/fs_data.h"
#include "../gen/tech01_data.h"
#include "../gen/campaign_data.h"
#include "../core/roster.h"
#include "../core/save.h"

#define PAL_TEXT 7u
#define N_LINES 9u
#define N_OFF (C->n_off)                                /* roster officers, as camp instances 0..n-1 */

enum { SC_MAIN, SC_EQUIP, SC_EXCH, SC_SHOP, SC_DEPLOY, SC_PRIZE };

#define officer(o) (o)
#define N_CMD 7u
static const uint16_t cmd_tx[N_CMD] = {TX_CMD_COUNCIL, TX_CMD_EQUIP, TX_CMD_EXCH, TX_CMD_SHOP, TX_CMD_DEPART, TX_CMD_PRIZE, TX_CMD_SAVE};
extern uint8_t cur_bt, camp_save_ok;
#define MSG_EFFECT 0xFFFFu        /* shop line 8: what the item under the cursor does (T-UI-05) */
uint8_t camp_council_done;                 /* set by a camp load / a return after defeat; consumed by camp_run */
void hud_item_effect(uint8_t line, uint8_t it) BANKED;

/* all camp state in one block (the C globals area is nearly full: this lives on the stack of camp_run) */
typedef struct {
    uint16_t *gold;
    CampDeploy *dep;
    uint8_t screen, cur, off, slot, pick, pick_off, pick_slot, sell, top, main_cur, council, ask;
    uint16_t msg;                                       /* text id of the status line (0 = default hint) */
    uint8_t n_off, mode, next, deploy_max, forced, forbid, camp;
} Camp;

static Camp *C;                                         /* 2 B global: points into camp_run's frame */
#define CD (camp_defs[C->camp])                         /* this camp's data (shop, council) */

/* no line may straddle tile ids 127/128: in LCDC.4 = 0 addressing they are not adjacent in VRAM
 * (0x97F0 vs 0x8800) and one 576 B GDMA would run on into the BG map at 0x9800 */
/* officer name by character (also officers who joined later), else the class name */
static uint16_t name_tx(uint8_t o)
{
    RosterRec r;
    uint16_t t;
    roster_read(o, &r);
    t = char_name_tx(r.character);
    return t ? t : class_name_tx[r.cls];
}

static uint8_t line_bank(uint8_t k) { return k < 6u ? 1u : 0u; }
static uint8_t line_id(uint8_t k)
{
    if (k < 3u) return (uint8_t)(k * 36u);                 /* bank1   0..107 */
    if (k < 6u) return (uint8_t)(128u + (k - 3u) * 36u);   /* bank1 128..235 */
    return (uint8_t)(1u + (k - 6u) * 36u);                 /* bank0   1..108 */
}

static void upload(uint8_t k, uint8_t lcd_off)
{
    uint8_t bank = line_bank(k);
    if (lcd_off) { VBK_REG = bank; set_bkg_data(line_id(k), 36, (uint8_t *)0xC800u); VBK_REG = 0; }
    else vb_gdma_wait(bank, 0xC800u, vb_slot_addr(line_id(k)), 576);
}

static void arrow(uint8_t on) { if (on) hud_tx(0, TX_ARROW, 0); else { hud_space(0); hud_space(0); } }

static void item_label(uint8_t unit, uint8_t s)
{
    uint8_t it = inv_get(unit, s);
    if (it == ITEM_NONE) { hud_tx(0, TX_EMPTY, 0); return; }
    hud_tx(0, item_name_tx[it], 0);
    if (inv_slot_equipped(unit, s)) { hud_space(0); hud_tx(0, TX_EQ_MARK, 0); }
}

/* index in the deployment order, or 0xFF */
static uint8_t dep_index(uint8_t o)
{
    uint8_t k;
    for (k = 0; k < C->dep->n; k++) if (C->dep->order[k] == o) return k;
    return 0xFF;
}

static void compose(uint8_t k)
{
    uint8_t u = officer(C->off);
    hud_begin();
    switch (C->screen) {
    case SC_MAIN:
        if (k == 0) {                                      /* T-SAVE-03: the battle ahead, the gold */
            uint16_t bt = bd_title(C->next);
            hud_tx(0, bt ? bt : TX_CAMP_TITLE, 0);    /* the gold is on the hint line: long titles cut it (B3) */
        }
        else if (k >= 1 && k <= N_CMD) {
            arrow(C->cur == k - 1u); hud_tx(0, cmd_tx[k - 1u], 0);
            if (k == 6) { hud_space(0); hud_num(0, prize_count()); }
            if (k == 1 && C->council) { hud_space(0); hud_tx(0, TX_DONE, 0); }
        } else if (k == 8) {
            if (C->msg) hud_tx(0, C->msg, 0);
            else { hud_tx(0, TX_GOLD, 0); hud_num(0, *C->gold); hud_space(0); hud_space(0); hud_tx(0, TX_CAMP_HINT, 0); }
        }
        break;
    case SC_EQUIP:
    case SC_EXCH:
        if (k == 0) {
            ActiveUnit a;
            unit_load(u, &a);
            hud_tx(0, C->screen == SC_EQUIP ? TX_CMD_EQUIP : TX_CMD_EXCH, 0); hud_space(0);
            hud_tx(0, name_tx(C->off), 0); hud_space(0);
            if (C->screen == SC_EQUIP || C->pick == 0xFF) {
                hud_tx(0, TX_ATK, 0); hud_num(0, a.atk); hud_space(0);
                hud_tx(0, TX_MAG, 0); hud_num(0, a.mag); hud_space(0);
                hud_tx(0, TX_MOVE, 0); hud_num(0, a.move);
            } else { hud_tx(0, TX_PICKED, 0); hud_tx(0, item_name_tx[inv_get(officer(C->pick_off), C->pick_slot)], 0); }
        } else if (k >= 1 && k <= 8) {
            uint8_t s = (uint8_t)(k - 1u);
            arrow(C->slot == s);
            item_label(u, s);
        }
        break;
    case SC_SHOP:
        if (k == 0) {
            hud_tx(0, TX_CMD_SHOP, 0); hud_space(0); hud_tx(0, C->sell ? TX_SELL : TX_BUY, 0); hud_space(0);
            hud_tx(0, TX_GOLD, 0); hud_num(0, *C->gold);
        } else if (!C->sell && k >= 1 && k <= 6) {
            uint8_t i = (uint8_t)(C->top + k - 1u);
            if (i < CD.n_shop) {
                uint8_t it = CD.shop[i];
                ItemDef d; item_def(it, &d);
                arrow(C->cur == i); hud_tx(0, item_name_tx[it], 0); hud_space(0); hud_num(0, d.price);
            }
        } else if (C->sell && k >= 1 && k <= 6) {
            uint8_t s = (uint8_t)(C->top + k - 1u);
            if (s < INV_SLOTS) {
                uint8_t it = inv_get(u, s);
                arrow(C->slot == s); item_label(u, s);
                if (it != ITEM_NONE && item_sell_price(it)) { hud_space(0); hud_num(0, item_sell_price(it)); }
            }
        } else if (k == 7) {
            hud_tx(0, C->sell ? TX_FROM : TX_GIVE, 0); hud_tx(0, name_tx(C->off), 0); hud_space(0);
            hud_num(0, inv_count(u)); hud_glyph(0, GLYPH_SLASH); hud_num(0, INV_SLOTS);
        } else if (k == 8) {
            uint8_t it = C->sell ? inv_get(u, C->slot) : CD.shop[C->cur];
            if (C->msg == MSG_EFFECT && it != ITEM_NONE) { hud_tx(0, item_name_tx[it], 0); hud_space(0); hud_item_effect(0, it); }
            else hud_tx(0, (C->msg && C->msg != MSG_EFFECT) ? C->msg : TX_SHOP_HINT2, 0);
        }
        break;
    case SC_DEPLOY:
        if (k == 0) { hud_tx(0, TX_CMD_DEPART, 0); hud_space(0); hud_num(0, C->dep->n); hud_glyph(0, GLYPH_SLASH); hud_num(0, C->deploy_max); }
        else if (k >= 1 && k <= 7 && (uint8_t)(C->top + k - 1u) < N_OFF) {
            uint8_t o = (uint8_t)(C->top + k - 1u), d = dep_index(o);
            arrow(C->cur == o); hud_tx(0, name_tx(o), 0); hud_space(0);
            if (d != 0xFF) { hud_tx(0, TX_DEP_MARK, 0); hud_num(0, (uint16_t)(d + 1u)); }
            if (o < 8u && (C->forced & (1u << o))) { hud_space(0); hud_tx(0, TX_FORCED, 0); }
        } else if (k == 8) hud_tx(0, C->msg ? C->msg : TX_DEPLOY_HINT, 0);
        break;
    case SC_PRIZE:                                         /* 7.4: distribute / sell / discard every prize */
        if (k == 0) { hud_tx(0, TX_CMD_PRIZE, 0); hud_space(0); hud_num(0, prize_count()); hud_space(0); hud_tx(0, TX_GOLD, 0); hud_num(0, *C->gold); }
        else if (k >= 1 && k <= 6) {
            uint8_t i = (uint8_t)(C->top + k - 1u);
            if (i < prize_count()) {
                uint8_t it = prize_get(i);
                arrow(C->cur == i); hud_tx(0, item_name_tx[it], 0);
                if (item_sell_price(it)) { hud_space(0); hud_num(0, item_sell_price(it)); }
            }
        } else if (k == 7) {
            hud_tx(0, TX_GIVE, 0); hud_tx(0, name_tx(C->off), 0); hud_space(0);
            hud_num(0, inv_count(u)); hud_glyph(0, GLYPH_SLASH); hud_num(0, INV_SLOTS);
        } else if (k == 8) hud_tx(0, C->msg ? C->msg : TX_PRIZE_HINT, 0);
        break;
    }
    hud_raster(0);
}

static void draw(uint8_t k) { compose(k); upload(k, 0); }

static void draw_all(uint8_t lcd_off)
{
    uint8_t k;
    for (k = 0; k < N_LINES; k++) { compose(k); upload(k, lcd_off); }
}

/* 回纹 border down both sides of the camp pages (bank0 tile 120 is free:
 * the text lines use bank0 1..108) */
static const uint8_t fret_v[16] = {0, 248, 0, 8, 0, 232, 0, 168, 0, 136, 0, 248, 0, 128, 0, 128};   /* 5 px key fret, 3 px clear of the text */
#define FRET_ID 120u

static void screen_init(void)
{
    uint8_t r, c, ids[20], at[20];
    uint16_t pal[4];
    if (LCDC_REG & LCDCF_ON) { vb_wait(); DISPLAY_OFF; }   /* after the council the LCD is already off:
                                                             LY stays 0 and vb_wait would never return */
    HIDE_SPRITES; HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    memset(ids, 0, 16);
    VBK_REG = 0; set_bkg_data(0, 1, ids);
    set_bkg_data(FRET_ID, 1, fret_v);
    for (r = 0; r < 18; r++) {
        uint8_t k = (uint8_t)(r >> 1);
        for (c = 0; c < 20; c++) {
            if (c == 0 || c == 19) { ids[c] = FRET_ID; at[c] = (uint8_t)(PAL_TEXT | (c ? 0x20u : 0u)); continue; }
            ids[c] = (uint8_t)(line_id(k) + (r & 1u) * 18u + (c - 1u));
            at[c] = (uint8_t)(PAL_TEXT | (line_bank(k) ? 0x08u : 0u));
        }
        VBK_REG = 1; set_bkg_tiles(0, r, 20, 1, at);
        VBK_REG = 0; set_bkg_tiles(0, r, 20, 1, ids);
    }
    pal[0] = fs_pals[0]; pal[1] = fs_pals[1]; pal[2] = fs_pals[2]; pal[3] = fs_pals[3];
    set_bkg_palette(PAL_TEXT, 1, pal);
    draw_all(1);
    DISPLAY_ON;
}

static void publish(void)
{
    uint8_t s;
    dbg_state = (uint8_t)(CAMP_DBG_STATE + C->screen);
    dbg_menu_item = C->screen == SC_MAIN || C->screen == SC_DEPLOY || C->screen == SC_PRIZE || (C->screen == SC_SHOP && !C->sell) ? C->cur : C->slot;
    dbg_sel = C->off;
    dbg_gold = *C->gold;
    for (s = 0; s < INV_SLOTS; s++) dbg_inv[s] = inv_get(0, s);
    dbg_frame_lo++;
}

static void say(uint16_t tx) { C->msg = tx; draw(8); }

static void enter(uint8_t sc)
{
    if (C->screen == SC_MAIN) C->main_cur = C->cur;     /* back in the menu on the command just used */
    C->screen = sc; C->cur = sc == SC_MAIN ? C->main_cur : 0; C->slot = 0; C->top = 0; C->msg = 0; C->pick = 0xFF; C->sell = 0;
    draw_all(0);
}

static void council(void)
{
    uint8_t i;
    for (i = 0; i < CD.n_council && i < DQ_MAX; i++) { DQ->speaker[i] = (uint8_t)CD.council[i][0]; DQ->text[i] = CD.council[i][1]; }
    DQ->n = i;
    dialog_run_from(0);
    DQ->n = 0;
    C->council = 1;
    screen_init();
}

static void shop_scroll(uint8_t sel, uint8_t n)
{
    (void)n;
    if (sel < C->top) C->top = sel;
    else if (sel >= C->top + 6u) C->top = (uint8_t)(sel - 5u);
}

/* returns 1 when the player departs */
static uint8_t step(uint8_t pressed)
{
    uint8_t up = pressed & J_UP, down = pressed & J_DOWN, left = pressed & J_LEFT, right = pressed & J_RIGHT;
    uint8_t u = officer(C->off);
    switch (C->screen) {
    case SC_MAIN:
        if (up || down) {
            uint8_t old = C->cur;
            C->cur = (uint8_t)(up ? (C->cur + N_CMD - 1u) % N_CMD : (C->cur + 1u) % N_CMD);
            draw((uint8_t)(old + 1u)); draw((uint8_t)(C->cur + 1u));
            if (C->msg) { C->msg = 0; draw(8); }
            sfx_play(SFX_CURSOR);
        } else if (pressed & J_A) {
            sfx_play(SFX_CONFIRM);
            if (C->cur == 0) council();
            else if (C->cur == 1) enter(SC_EQUIP);
            else if (C->cur == 2) enter(SC_EXCH);
            else if (C->cur == 3) enter(SC_SHOP);
            else if (C->cur == 5) enter(SC_PRIZE);
            else if (C->cur == 6) {                         /* 存档: camp save (11.6) of the synced roster */
                SaveUi ui;
                uint8_t o;
                for (o = 0; o < C->n_off; o++) roster_sync(o, o);
                memset(&ui, 0, sizeof ui);
                ui.battle_id = cur_bt; ui.gold = *C->gold; ui.options = 1;
                ui.presentation = C->council;            /* T-SAVE-01: bit0 = 军议已毕 */
                if (save_camp(SAVE_SLOT_NORMAL0, &ui) == SAVE_OK) { camp_save_ok = 1; say(TX_CAMP_SAVED); }
                else say(TX_SAVE_FAIL);
                dbg_saves++;
            }
            else if (prize_count()) { sfx_play(SFX_ERROR); say(TX_PRIZE_FIRST); }    /* 7.4: queue must be empty */
            else if (!C->council && CD.n_council) { sfx_play(SFX_ERROR); say(TX_COUNCIL_FIRST); }
            else enter(SC_DEPLOY);
        }
        break;
    case SC_EQUIP:
    case SC_EXCH:
        if (up || down) {
            uint8_t old = C->slot;
            C->slot = (uint8_t)(up ? (C->slot + 7u) & 7u : (C->slot + 1u) & 7u);
            draw((uint8_t)(old + 1u)); draw((uint8_t)(C->slot + 1u));
        } else if (left || right) {
            C->off = (uint8_t)(left ? (C->off + N_OFF - 1u) % N_OFF : (C->off + 1u) % N_OFF);
            draw_all(0);
        } else if (pressed & J_B) {
            if (C->screen == SC_EXCH && C->pick != 0xFF) { C->pick = 0xFF; draw(0); }
            else enter(SC_MAIN);
        } else if ((pressed & J_A) && C->screen == SC_EQUIP) {
            uint8_t it = inv_get(u, C->slot), cat = item_category(it);
            if (it == ITEM_NONE || cat == EQ_NONE) sfx_play(SFX_ERROR);
            else if (inv_slot_equipped(u, C->slot)) { item_unequip(u, cat); sfx_play(SFX_CONFIRM); }
            else if (item_equip(u, C->slot) == IR_OK) sfx_play(SFX_CONFIRM);
            else sfx_play(SFX_ERROR);
            draw(0); draw((uint8_t)(C->slot + 1u));
            { uint8_t s; for (s = 0; s < INV_SLOTS; s++) if (s != C->slot) draw((uint8_t)(s + 1u)); }   /* replaced marks */
        } else if (pressed & J_A) {                       /* 交换: first pick, then the other end */
            if (C->pick == 0xFF) {
                if (inv_get(u, C->slot) == ITEM_NONE) { sfx_play(SFX_ERROR); break; }
                C->pick = 1; C->pick_off = C->off; C->pick_slot = C->slot;
                sfx_play(SFX_CONFIRM); draw(0);
            } else {
                if (item_exchange_camp(officer(C->pick_off), C->pick_slot, u, C->slot) == IR_OK) sfx_play(SFX_CONFIRM);
                else sfx_play(SFX_ERROR);
                C->pick = 0xFF;
                draw_all(0);
            }
        }
        break;
    case SC_SHOP:
        if (pressed & J_SELECT) { C->sell ^= 1u; C->cur = 0; C->slot = 0; C->top = 0; C->msg = 0; draw_all(0); }
        else if (left || right) {
            C->off = (uint8_t)(left ? (C->off + N_OFF - 1u) % N_OFF : (C->off + 1u) % N_OFF);
            C->msg = 0; draw_all(0);
        } else if (up || down) {
            uint8_t top0 = C->top, old = C->sell ? C->slot : C->cur, now;
            if (C->sell) { C->slot = (uint8_t)(up ? (C->slot + 7u) & 7u : (C->slot + 1u) & 7u); shop_scroll(C->slot, INV_SLOTS); now = C->slot; }
            else { C->cur = (uint8_t)(up ? (C->cur + CD.n_shop - 1u) % CD.n_shop : (C->cur + 1u) % CD.n_shop); shop_scroll(C->cur, CD.n_shop); now = C->cur; }
            if (C->top != top0) { uint8_t k; for (k = 1; k <= 6; k++) draw(k); }   /* scrolled */
            else { draw((uint8_t)(old - C->top + 1u)); draw((uint8_t)(now - C->top + 1u)); }
            C->msg = MSG_EFFECT; draw(8);
        } else if (pressed & J_B) enter(SC_MAIN);
        else if ((pressed & J_A) && !C->sell) {           /* 7.3: check gold and a free slot, then pay and give */
            uint8_t it = CD.shop[C->cur];
            ItemDef d;
            uint16_t price;
            item_def(it, &d); price = d.price;
            if (*C->gold < price) { sfx_play(SFX_ERROR); say(TX_NO_GOLD); }
            else if (inv_count(u) >= INV_SLOTS) { sfx_play(SFX_ERROR); say(TX_BAG_FULL); }
            else { *C->gold = (uint16_t)(*C->gold - price); inv_add(u, it); sfx_play(SFX_CONFIRM); C->msg = TX_BOUGHT; draw(0); draw(7); draw(8); }
        } else if (pressed & J_A) {
            uint8_t it = inv_get(u, C->slot), eq = inv_slot_equipped(u, C->slot);
            uint16_t p = item_sell_price(it);
            if (it == ITEM_NONE || !p) { sfx_play(SFX_ERROR); say(TX_CANT_SELL); }
            else {
                inv_set(u, C->slot, ITEM_NONE);              /* drops the equip mark too */
                if (eq) item_rederive(u);
                *C->gold = (uint16_t)(*C->gold + p);
                sfx_play(SFX_CONFIRM); C->msg = TX_SOLD; draw_all(0);
            }
        }
        break;
    case SC_PRIZE: {
        uint8_t n = prize_count();
        if (C->mode == CAMP_PRIZES_ONLY && !n) return 1;                    /* continuous battle: go on */
        if ((pressed & J_START) && C->mode != CAMP_PRIZES_ONLY) { enter(SC_MAIN); break; }   /* make room */
        if (!n) { enter(SC_MAIN); break; }
        if (left || right) {
            C->off = (uint8_t)(left ? (C->off + N_OFF - 1u) % N_OFF : (C->off + 1u) % N_OFF);
            C->msg = 0; C->ask = 0; draw(7); draw(8);
        } else if (up || down) {
            C->cur = (uint8_t)(up ? (C->cur + n - 1u) % n : (C->cur + 1u) % n);
            shop_scroll(C->cur, n);
            C->msg = 0; C->ask = 0;
            draw_all(0);
        } else if (pressed & (J_A | J_B | J_SELECT)) {
            uint8_t it = prize_get(C->cur), ok = 0;
            /* T-UI-03: selling or discarding needs the same key twice; anything else cancels */
            uint8_t want = (pressed & J_B) ? 1u : ((pressed & J_SELECT) ? 2u : 0u);
            if (want && C->ask != want && (want == 1u ? item_kind(it) != IK_PLOT : item_sell_price(it) != 0)) {
                C->ask = want; sfx_play(SFX_CURSOR);
                say(item_kind(it) >= IK_WEAPON ? TX_ASK_RARE : (want == 1u ? TX_ASK_DISCARD : TX_ASK_SELL));
                break;
            }
            C->ask = 0;
            if (pressed & J_A) {
                if (inv_count(u) >= INV_SLOTS) say(TX_BAG_FULL);
                else { inv_add(u, it); ok = 1; C->msg = 0; }
            } else if (pressed & J_SELECT) {
                uint16_t p = item_sell_price(it);
                if (!p) say(TX_CANT_SELL);
                else { *C->gold = (uint16_t)(*C->gold + p); ok = 1; C->msg = TX_SOLD; }
            } else {
                ItemDef d;
                item_def(it, &d);
                if (d.kind == IK_PLOT) say(TX_NO_DISCARD);       /* plot items must be kept */
                else { ok = 1; C->msg = TX_DISCARDED; }
            }
            if (ok) {
                prize_remove(C->cur);
                sfx_play(SFX_CONFIRM);
                if (!prize_count()) { if (C->mode == CAMP_PRIZES_ONLY) return 1; enter(SC_MAIN); break; }
                if (C->cur >= prize_count()) C->cur = (uint8_t)(prize_count() - 1u);
                shop_scroll(C->cur, prize_count());
                draw_all(0);
            } else sfx_play(SFX_ERROR);
        }
        break;
    }
    case SC_DEPLOY:
        if (up || down) {
            uint8_t old = C->cur, top0 = C->top;
            C->cur = (uint8_t)(up ? (C->cur + N_OFF - 1u) % N_OFF : (C->cur + 1u) % N_OFF);
            if (C->cur < C->top) C->top = C->cur;                    /* 7 lines: keep the cursor in view */
            else if (C->cur >= C->top + 7u) C->top = (uint8_t)(C->cur - 6u);
            if (C->top != top0) { uint8_t k; for (k = 1; k <= 7; k++) draw(k); }
            else { draw((uint8_t)(old - C->top + 1u)); draw((uint8_t)(C->cur - C->top + 1u)); }
        } else if (pressed & J_A) {
            uint8_t d = dep_index(C->cur), k;
            if (d != 0xFF) {
                if (C->forced & (1u << C->cur)) { sfx_play(SFX_ERROR); say(TX_FORCED_MSG); break; }
                for (k = d; k + 1u < C->dep->n; k++) C->dep->order[k] = C->dep->order[k + 1u];
                C->dep->n--;
            } else {
                if (!roster_available(C->cur)) { sfx_play(SFX_ERROR); say(TX_LEFT_MSG); break; }   /* 2.4 */
                if (C->cur < 8u && ((C->forbid >> C->cur) & 1u)) { sfx_play(SFX_ERROR); say(TX_STAY_MSG); break; }
                if (C->dep->n >= C->deploy_max) { sfx_play(SFX_ERROR); break; }
                C->dep->order[C->dep->n++] = C->cur;
            }
            sfx_play(SFX_CONFIRM); C->msg = 0;
            draw_all(0);
        } else if (pressed & J_SELECT) {                  /* 调换位置: move the officer one slot earlier */
            uint8_t d = dep_index(C->cur), tmp;
            if (d == 0xFF || d == 0) { sfx_play(SFX_ERROR); break; }
            tmp = C->dep->order[d - 1u]; C->dep->order[d - 1u] = C->dep->order[d]; C->dep->order[d] = tmp;
            sfx_play(SFX_CONFIRM); draw_all(0);
        } else if (pressed & J_B) enter(SC_MAIN);
        else if (pressed & J_START) {
            if (!C->dep->n) { sfx_play(SFX_ERROR); break; }
            sfx_play(SFX_CONFIRM);
            return 1;
        }
        break;
    }
    return 0;
}

void camp_run(uint16_t *gold, CampDeploy *dep, uint8_t mode, uint8_t next_bt, uint8_t camp_idx) BANKED
{
    Camp camp;
    memset(&camp, 0, sizeof camp);
    camp.gold = gold; camp.dep = dep; camp.pick = 0xFF; camp.mode = mode; camp.next = next_bt;
    camp.camp = camp_idx < N_CAMPS ? camp_idx : 0u;
    if (mode == CAMP_FULL && camp_defs[camp.camp].join != 0xFFu) join_officer(camp_defs[camp.camp].join);   /* e.g. 三顾茅庐 (2.4 JOIN) */
    camp.n_off = roster_count() < CAMP_MAX_OFF ? roster_count() : CAMP_MAX_OFF;   /* the deployment list scrolls */
    camp.council = camp_council_done; camp_council_done = 0;   /* loaded save / back from a defeat */
    camp.deploy_max = 4; camp.forced = 1;                   /* no next battle (ending) */
    bd_deploy_rule(next_bt, &camp.deploy_max, &camp.forced, &camp.forbid);   /* battle tables: bank 14 */
    if (camp.deploy_max > CAMP_MAX_DEPLOY) camp.deploy_max = CAMP_MAX_DEPLOY;
    if (mode == CAMP_PRIZES_ONLY && !prize_count()) return;
    C = &camp;
    {   /* first visit: everyone, in roster order; officers the story keeps behind leave the list */
        uint8_t k, n = 0;
        if (!dep->n) { for (k = 0; k < N_OFF && n < C->deploy_max; k++) if (roster_available(k) && (k >= 8u || !((camp.forbid >> k) & 1u))) dep->order[n++] = k; }
        else for (k = 0; k < dep->n; k++) if (dep->order[k] >= 8u || !((camp.forbid >> dep->order[k]) & 1u)) dep->order[n++] = dep->order[k];
        dep->n = n;
    }
    if (prize_count()) { camp.screen = SC_PRIZE; }        /* 7.4: the spoils come first */
    screen_init();
    input_flush();
    for (;;) {
        uint8_t pressed;
        vb_wait();
        input_update();                    /* VBlank-ISR latched edges: no press is lost while drawing */
        pressed = in_pressed;
        if (in_dy < 0) pressed |= J_UP; else if (in_dy > 0) pressed |= J_DOWN;
        if (in_dx < 0) pressed |= J_LEFT; else if (in_dx > 0) pressed |= J_RIGHT;
        if (pressed && step(pressed)) break;
        publish();
    }
    input_flush();
    vb_wait();
    DISPLAY_OFF;                                        /* the caller rebuilds map mode (12.5) */
    C = 0;
}
