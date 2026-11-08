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
#include "../core/vm.h"
#include "../gen/tech01_data.h"
#include "../gen/text_data.h"

#define W7_AI ((AiScratch *)0xDC00)
typedef char ai_fits_bank7[(sizeof(AiScratch) <= 0x400u) ? 1 : -1];

void units_clear(void);
void units_snapshot(uint8_t n);
#define MAX_SHOWN 10u               /* OBJ placeholders: up to 10 units (20 OBJ + cursor) */
extern uint8_t snap_x[MAX_SHOWN], snap_y[MAX_SHOWN], snap_fac[MAX_SHOWN], snap_flags[MAX_SHOWN];
extern uint16_t snap_hp[MAX_SHOWN];
void vm_clear_battle_flags(void);
#define SNAP_ALIVE(i) (snap_hp[i] && !(snap_flags[i] & (AF_RETREAT | AF_DEAD | AF_AWAY)))

enum {
    G_IDLE, G_MOVE, G_MENU, G_TARGET, G_SHOW, G_ENEMY, G_BANNER, G_OUTCOME, G_TITLE
};

#define OPT_AUTOSAVE 0x01u        /* design doc 11.6: autosave at player phase start, default on */

static Battle B;
static uint8_t state, sel, cur_x, cur_y, tent_x, tent_y, menu, ntarget, tsel, timer, next_state;
static uint8_t targets[8];
static uint8_t enemy_i, options, autosave_pending, pending_start, end_fired;   /* no initialised statics */
static uint8_t hud_dirty, last_hx, last_hy, hud_settle;
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
    return class_name_tx[u->class_id];
}

static void place_sprites(void)
{
    uint8_t i, s = 0;
    units_snapshot(B.n_units);
    for (i = 0; i < MAX_SHOWN; i++, s += 2) {
        int16_t ux, uy, sx, sy;
        uint8_t base, pal;
        if (i >= B.n_units || !SNAP_ALIVE(i)) { move_sprite(s, 0, 0); move_sprite(s + 1, 0, 0); continue; }
        ux = snap_x[i]; uy = snap_y[i];
        if ((state == G_MENU || state == G_TARGET) && i == sel) { ux = tent_x; uy = tent_y; }
        sx = ux * 16 - (int16_t)cam_x + 8;
        sy = uy * 16 - (int16_t)cam_y + 16;
        base = snap_fac[i] == FAC_ENEMY ? OBJT_ENEMY : OBJT_PLAYER;
        pal = snap_fac[i] == FAC_ENEMY ? 1 : ((snap_flags[i] & AF_ACTED) ? 3 : 0);
        set_sprite_tile(s, base); set_sprite_tile(s + 1, (uint8_t)(base + 2));
        set_sprite_prop(s, pal); set_sprite_prop(s + 1, pal);
        if (sx < -8 || sx > 168 || sy < 0 || sy >= (int16_t)VIEW_H + 16) {
            move_sprite(s, 0, 0); move_sprite(s + 1, 0, 0);
        } else {
            move_sprite(s, (uint8_t)sx, (uint8_t)sy);
            move_sprite(s + 1, (uint8_t)(sx + 8), (uint8_t)sy);
        }
    }
    {
        int16_t sx = (int16_t)cur_x * 16 - (int16_t)cam_x + 8;
        int16_t sy = (int16_t)cur_y * 16 - (int16_t)cam_y + 16;
        uint8_t hide = state == G_ENEMY || state == G_BANNER || state == G_OUTCOME;
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

static void hud_menu(void)
{
    hud_begin();
    if (ntarget) {
        if (menu == 0) hud_tx(0, TX_ARROW, 0); else { hud_space(0); hud_space(0); }
        hud_tx(0, TX_ACT_ATTACK, 0); hud_space(0); hud_space(0);
    }
    if (menu == 1 || !ntarget) hud_tx(0, TX_ARROW, 0); else { hud_space(0); hud_space(0); }
    hud_tx(0, TX_ACT_WAIT, 0);
    hud_tx(1, TX_MOVE_HINT, 0);
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

/* run the event VM for one fact; play queued dialogue (design doc 9) */
static void fire(uint8_t trig, uint8_t phase, uint8_t actor, uint8_t target, uint8_t unit)
{
    VmFact f;
    uint8_t r;
    uint8_t n_before = B.n_units, alive_before = count_alive();
    f.trigger = trig; f.phase = phase; f.actor = actor; f.target = target; f.unit = unit;
    r = vm_fire(&B, &f);
    if (r != VM_OK) { dbg_vm_err = r; B.outcome = OUT_LOSE; }   /* BAD_SCRIPT: safe stop (9.3) */
    if (B.n_units != n_before || count_alive() != alive_before) {
        uint8_t o = SVBK_REG;                   /* spawns/retreats change the approach-field sources */
        SVBK_REG = WB_SCRATCH; ai_invalidate(W7_AI); SVBK_REG = o;
    }
    if (DQ->n) { dialog_run(); map_mode_restore(); }
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
    ui.presentation = 0; ui.text_speed = 0; ui.options = options; ui.gold = 0;
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
                menu = ntarget ? 0 : 1;
                state = G_MENU;
                mapview_highlight(0);
                hud_menu();
            }
        }
        break;
    case G_MENU:
        if (ntarget && (in_dx || in_dy)) { menu ^= 1; hud_menu(); }
        else if (in_pressed & J_B) { state = G_MOVE; mapview_highlight(1); hud_begin(); hud_tx(0, TX_MOVE_HINT, 0); hud_end(); }
        else if (in_pressed & J_A) {
            if (menu == 0 && ntarget) { tsel = 0; state = G_TARGET; cur_x = tent_x; cur_y = tent_y; hud_target(); }
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

static void enemy_step(void)
{
    ActiveUnit u;
    AiCommand c;
    AttackResult r;
    uint8_t o;
    uint16_t t0;
    while (enemy_i < B.n_units) {
        load(enemy_i, &u);
        if (u.faction == FAC_ENEMY && battle_can_act(&u)) break;
        enemy_i++;
    }
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
    o = SVBK_REG;
    t0 = sys_time;
    SVBK_REG = WB_SCRATCH;
    ai_decide(&B, W7_AI, W7_MR, enemy_i, AI_AGGRESSIVE, &c);
    SVBK_REG = o;
    dbg_t_ai = sys_time - t0;
    if (c.x != u.x || c.y != u.y) {
        battle_move(enemy_i, c.x, c.y);
        fire(TR_POST_MOVE, FAC_ENEMY, enemy_i, 0xFF, 0xFF);
        load(enemy_i, &u);
        if (B.outcome || !unit_alive(&u)) c.action = ACT_WAIT;   /* 4.3: the move's event ended the action */
    }
    cur_x = c.x; cur_y = c.y;                  /* camera follows the actor */
    if (c.action == ACT_ATTACK) {
        attack_and_present(enemy_i, c.target, &r);
        hud_result(&r);
        timer = 50;
    } else {
        load(enemy_i, &u); u.action_flags |= AF_ACTED; unit_store(enemy_i, &u);
        timer = 20;
        hud_begin(); hud_end();
    }
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
    if (B.outcome && state != G_OUTCOME && state != G_SHOW && state != G_TITLE) {
        state = G_OUTCOME;
        save_invalidate(SAVE_SLOT_SUSPEND);     /* never "continue" a finished battle */
        hud_begin(); hud_tx(0, B.outcome == OUT_WIN ? TX_WIN : TX_LOSE, 0); hud_end();
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
        if (in_pressed & J_A) {
            DISPLAY_OFF;
            start_battle(); mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
            DISPLAY_ON;
        }
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
    /* cursor info HUD: refresh once the cursor has rested 3 frames (no re-raster on every step while scrolling) */
    if (state == G_IDLE) {
        if (cur_x != last_hx || cur_y != last_hy) { last_hx = cur_x; last_hy = cur_y; hud_settle = 3; hud_dirty = 1; }
        else if (hud_settle) hud_settle--;
        if (hud_dirty && !hud_settle) { hud_unit_or_terrain(); hud_dirty = 0; }
    }
}

static void publish_debug(void)
{
    uint8_t i;
    dbg_state = state; dbg_sel = sel; dbg_cur_x = cur_x; dbg_cur_y = cur_y;
    dbg_cam_x = cam_x; dbg_cam_y = cam_y;
    dbg_outcome = B.outcome; dbg_round = B.round; dbg_phase = B.phase;
    dbg_rng = B.rng.state; dbg_draws = B.rng.draws;
    for (i = 0; i < MAX_SHOWN; i++) {             /* snapshot taken by place_sprites this frame */
        uint8_t live = i < B.n_units;
        dbg_unit_xy[i * 2] = live ? snap_x[i] : 0; dbg_unit_xy[i * 2 + 1] = live ? snap_y[i] : 0;
        dbg_unit_hp[i] = live ? snap_hp[i] : 0;
    }
    dbg_n_units = B.n_units;
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
