#pragma bank 5   /* game logic, HUD and map view: code bank 5 (design doc 11.3) */
#include "game.h"

Battle B;
uint8_t state, sel, cur_x, cur_y, tent_x, tent_y, menu, ntarget, tsel, timer, next_state;
uint8_t xr_list[4], n_xr, xri, rslot;
uint8_t danger_on, dz_i;                /* enemy danger view (8.4): shown / next unit to add */
uint8_t mitems[6], isel, keep_inv, n_mitems, n_slist, ssel, aim_x, aim_y, aim_dirty, n_talk;
uint8_t enemy_i, options, autosave_pending, pending_start, end_fired;   /* no initialised statics */
uint8_t hud_dirty, last_hx, last_hy, hud_settle;
uint16_t carry_round;                   /* 2.4 round_mode CARRY */
uint8_t camp_save_ok;
uint8_t audio_ready;                    /* music_play only after audio_init */                          /* a valid camp save exists (title option; camp.c sets it) */
CampDeploy deploy;                      /* officer order chosen in the camp (2.3); n = 0: default */
AiMode ai_mode;                         /* static: enemy_step sits above the deepest call chain */
static void publish_debug(void);

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
         || state == G_IAIM || state == G_XLIST || state == G_XRECV) && sel < MAX_SHOWN) {
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
        n_xr = 0;
        {
            uint8_t mine = inv_count(sel), theirs = 0;
            if (mine) mitems[n_mitems++] = MI_ITEM;
            for (k = 0; k < 4; k++) {
                int16_t x = (int16_t)tent_x + DX[k], y = (int16_t)tent_y + DY[k];
                int8_t u;
                ActiveUnit r;
                if (x < 0 || y < 0 || x >= (int16_t)grid_w || y >= (int16_t)grid_h) continue;
                u = unit_at((uint8_t)x, (uint8_t)y);
                if (u < 0 || (uint8_t)u == sel) continue;
                load((uint8_t)u, &r);
                if (factions_hostile(r.faction, a.faction)) continue;
                xr_list[n_xr++] = (uint8_t)u;
                theirs |= inv_count((uint8_t)u);
            }
            if (n_xr && (mine || theirs)) mitems[n_mitems++] = MI_EXCH;
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

void publish_inv(void) BANKED
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

static void hud_msg(uint8_t tx) { hud_begin(); hud_tx(1, tx, 0); hud_end(); }

static void hud_slot(uint8_t line, uint8_t unit, uint8_t s)
{
    uint8_t it = inv_get(unit, s);
    hud_tx(line, TX_ARROW, 0);
    if (it == ITEM_NONE) hud_tx(line, TX_EMPTY, 0); else hud_tx(line, item_name_tx[it], 0);
    if (inv_slot_equipped(unit, s)) { hud_space(line); hud_tx(line, TX_EQ_MARK, 0); }
    hud_space(line); hud_num(line, (uint16_t)(s + 1u)); hud_glyph(line, GLYPH_SLASH); hud_num(line, INV_SLOTS);
}

static void hud_xown(void)
{
    hud_begin();
    hud_slot(0, sel, isel);
    hud_tx(1, TX_XOWN_HINT, 0);
    hud_end();
}

static void hud_xrecv(void)
{
    ActiveUnit r;
    uint8_t u = xr_list[xri];
    load(u, &r);
    hud_begin();
    hud_tx(0, unit_name_tx(&r), 0); hud_space(0); hud_slot(0, u, rslot);
    hud_tx(1, TX_XRECV_HINT, 0);
    hud_end();
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

void hud_item_result(const ItemResult *r) BANKED
{
    uint8_t k;
    hud_begin();
    if (r->item != ITEM_NONE) {
        { ItemDef d; item_def(r->item, &d); k = d.kind; }
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

void hud_strat_result(uint8_t sid, const StrategyResult *sr) BANKED
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

void hud_result(const AttackResult *r) BANKED
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

void hud_banner(uint8_t phase) BANKED
{
    hud_begin();
    hud_tx(0, phase == FAC_ENEMY ? TX_PHASE_ENEMY : TX_PHASE_PLAYER, B.round);
    hud_end();
}

/* ---------------------------------------------------------------- presentation (design doc 12.4/12.5) */
void map_mode_restore(void) BANKED
{
    /* LCD is off (fs_present returns that way): reload map-mode VRAM, palettes, HUD */
    set_bkg_palette(0, 8, tech01_bg_pals);
    VBK_REG = VBK_TILES;
    rom_set_bkg_data(TECH01_BANK, 0, TECH01_BG_TILES, tech01_bg_tiles);
    VBK_REG = 1; set_bkg_data(HUD_BLANK, 1, blank16); VBK_REG = VBK_TILES;
    hud_init(HUD_BLANK);
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
void play_queue(void) BANKED
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
void fire(uint8_t trig, uint8_t phase, uint8_t actor, uint8_t target, uint8_t unit) BANKED
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
void attack_and_present(uint8_t ai, uint8_t di, AttackResult *r) BANKED
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



/* ---------------------------------------------------------------- flow */

void danger_hide(void) BANKED
{
    uint8_t o;
    if (!danger_on) return;
    danger_on = 0;
    mapview_danger(0);
    o = SVBK_REG; SVBK_REG = WB_SCRATCH; ai_invalidate(W7_AI); SVBK_REG = o;
}




/* a new campaign (2.1): starting roster with its items, battle 1 */




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
    danger_hide();
    mapview_highlight(1);
    hud_begin(); hud_tx(0, TX_MOVE_HINT, 0); hud_end();
}

/* one main-loop frame without game logic (blocking prompts keep scrolling, HUD and the map alive) */
static void frame_tick(void)
{
    input_update();
    mapview_camera_follow(cur_x, cur_y);
    mapview_prepare();
    hud_service();
    place_sprites();
    publish_debug();
    wait_vbl_done();
    hud_commit();
    mapview_commit();
}

static void hold_frames(uint8_t n) { while (n--) { frame_tick(); if (in_pressed & (J_A | J_B)) break; } }

/* 7.4: the mover is full -> the player picks any officer with a free slot (left/right, A) */
static uint8_t choose_receiver(uint8_t item, uint8_t who)
{
    ActiveUnit u;
    for (;;) {
        load(who, &u);
        hud_begin();
        hud_tx(0, TX_TREASURE, 0); hud_tx(0, item_name_tx[item], 0); hud_space(0); hud_tx(0, TX_GIVE, 0);
        hud_tx(1, TX_ARROW, 0); hud_tx(1, unit_name_tx(&u), 0); hud_space(1);
        hud_num(1, inv_count(who)); hud_glyph(1, GLYPH_SLASH); hud_num(1, INV_SLOTS); hud_space(1); hud_tx(1, TX_PICK_HINT, 0);
        hud_end();
        for (;;) {
            frame_tick();
            if (in_pressed & J_A) { sfx_play(SFX_CONFIRM); return who; }
            if (in_dx || in_dy) {
                uint8_t k, n = who;
                for (k = 0; k < B.n_units; k++) {
                    n = (uint8_t)((n + (in_dx < 0 || in_dy < 0 ? B.n_units - 1u : 1u)) % B.n_units);
                    load(n, &u);
                    if (unit_alive(&u) && u.faction == FAC_PLAYER && inv_count(n) < INV_SLOTS) break;
                }
                who = n;
                break;
            }
        }
    }
}

void publish_treasures(void) BANKED
{
    uint8_t i, m = 0;
    TreasureDef d;
    for (i = 0; treasure_def(i, &d) && i < 8; i++) if (treasure_at(d.x, d.y) == i) m |= (uint8_t)(1u << i);
    dbg_treasure_mask = m;
}

/* 7.4: a player unit that logically enters a treasure cell (POST_MOVE) claims it once */
static void treasure_pickup(uint8_t x, uint8_t y)
{
    uint8_t t = treasure_at(x, y), who;
    TreasureDef d;
    ActiveUnit u;
    if (t == 0xFF) return;
    treasure_def(t, &d);
    who = inv_count(sel) < INV_SLOTS ? sel : inv_first_free(&B, FAC_PLAYER);
    if (who != sel && who != 0xFF) who = choose_receiver(d.item, who);
    hud_begin();
    if (who == 0xFF) {                         /* everyone full: the treasure stays, nothing is marked */
        hud_tx(0, TX_TREASURE, 0); hud_tx(0, item_name_tx[d.item], 0); hud_tx(1, TX_TREASURE_STAYS, 0);
        sfx_play(SFX_ERROR);
    } else {
        treasure_take(t, who);
        load(who, &u);
        hud_tx(0, TX_GOT, 0); hud_tx(0, item_name_tx[d.item], 0); hud_tx(1, TX_GIVE, 0); hud_tx(1, unit_name_tx(&u), 0);
        sfx_play(SFX_CONFIRM);
        dbg_treasures++;
        mapview_treasures();
        publish_inv();
    }
    hud_end();
    publish_treasures();
    hold_frames(45);
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
        treasure_pickup(tent_x, tent_y);
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
    uint8_t recv = xr_list[xri], ia = inv_get(sel, isel), ib = inv_get(recv, rslot);
    if (commit_move_if_needed()) {
        if (item_exchange(sel, isel, recv, rslot) == IR_OK) {
            load(recv, &r);
            hud_begin();
            hud_tx(0, TX_ACT_EXCH, 0); hud_space(0); hud_tx(0, unit_name_tx(&r), 0);
            if (ia != ITEM_NONE) { hud_tx(1, TX_GIVE, 0); hud_tx(1, item_name_tx[ia], 0); hud_space(1); }
            if (ib != ITEM_NONE) { hud_tx(1, TX_GOT, 0); hud_tx(1, item_name_tx[ib], 0); }
            hud_end();
            sfx_play(SFX_CONFIRM);
            publish_inv();
        } else { load(sel, &r); r.action_flags |= AF_ACTED; unit_store(sel, &r); }   /* POST_MOVE changed things */
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
        } else if ((in_pressed & J_B) && danger_on) {
            danger_hide(); hud_dirty = 1;
        } else if (in_pressed & J_B) {          /* 8.4: exact enemy danger view, one enemy per frame */
            uint8_t o = SVBK_REG;
            SVBK_REG = WB_SCRATCH; memset(W7_DANGER, 0, 512); SVBK_REG = o;
            dz_i = 0; state = G_DANGER;
            hud_begin(); hud_tx(0, TX_DANGER_CALC, 0); hud_end();
        } else if (in_pressed & J_START) {
            begin_enemy_phase();               /* end turn */
        } else if (in_pressed & J_SELECT) {
            uint8_t r = do_suspend_save();     /* only in the stable player idle state (11.6) */
            hud_begin(); hud_tx(0, r == SAVE_OK ? TX_SAVED : TX_SAVE_FAIL, 0); hud_end();
            hud_settle = 90; hud_dirty = 1;    /* keep the message up for a moment */
        }
        break;
    case G_DANGER:
        while (dz_i < B.n_units) {
            ActiveUnit e;
            load(dz_i, &e);
            dz_i++;
            if (!unit_alive(&e) || e.faction != FAC_ENEMY) continue;
            {
                uint8_t o = SVBK_REG;
                SVBK_REG = WB_SCRATCH;
                ai_danger_add(&B, W7_MR, (uint8_t)(dz_i - 1u), W7_DANGER);
                SVBK_REG = o;
            }
            break;                             /* one enemy per frame: the map keeps scrolling */
        }
        if (dz_i >= B.n_units) {
            danger_on = 1; mapview_danger(1); state = G_IDLE;
            hud_begin(); hud_tx(0, TX_DANGER_ON, 0); hud_end();
            hud_settle = 90; hud_dirty = 1;
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
            else if (it == MI_EXCH) { isel = 0; state = G_XLIST; hud_xown(); }
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
        if (in_dx > 0 || in_dy > 0) { isel = next_item(isel, 1); hud_ilist(TX_ITEM_HINT); }
        else if (in_dx < 0 || in_dy < 0) { isel = next_item(isel, -1); hud_ilist(TX_ITEM_HINT); }
        else if (in_pressed & J_B) { state = G_MENU; hud_menu(); }
        else if (in_pressed & J_A) { aim_x = tent_x; aim_y = tent_y; state = G_IAIM; aim_dirty = 1; sfx_play(SFX_CONFIRM); }
        break;
    case G_XLIST:                                  /* 交换 1/2: own slot (may be empty: take an item) */
        if (in_dx > 0 || in_dy > 0) { isel = (uint8_t)((isel + 1u) & 7u); hud_xown(); }
        else if (in_dx < 0 || in_dy < 0) { isel = (uint8_t)((isel + 7u) & 7u); hud_xown(); }
        else if (in_pressed & J_B) { state = G_MENU; hud_menu(); }
        else if (in_pressed & J_A) {
            if (inv_slot_equipped(sel, isel)) { sfx_play(SFX_ERROR); hud_msg(TX_IR_6); }
            else { xri = 0; rslot = 0; state = G_XRECV; sfx_play(SFX_CONFIRM); hud_xrecv(); }
        }
        break;
    case G_XRECV:                                  /* 交换 2/2: left/right ally, up/down its slot */
        if (in_dx) { xri = (uint8_t)((xri + (in_dx > 0 ? 1u : n_xr - 1u)) % n_xr); hud_xrecv(); }
        else if (in_dy) { rslot = (uint8_t)((rslot + (in_dy > 0 ? 1u : 7u)) & 7u); hud_xrecv(); }
        else if (in_pressed & J_B) { state = G_XLIST; hud_xown(); }
        else if (in_pressed & J_A) {
            uint8_t r = xr_list[xri];
            if (inv_get(sel, isel) == ITEM_NONE && inv_get(r, rslot) == ITEM_NONE) sfx_play(SFX_ERROR);
            else if (inv_slot_equipped(r, rslot)) { sfx_play(SFX_ERROR); hud_msg(TX_IR_6); }
            else do_exchange();
        }
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
















static void logic(void)
{
    if (pending_start && state == G_BANNER) {
        pending_start = 0;
        write_snapshot();                      /* 11.6: before BATTLE_START, from the top of the stack */
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
        timer = 40;                            /* ignore presses still in flight from the last action */
        input_flush();
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
            ready = ai_field_pump(W7_AI, (state == G_SHOW && next_state == G_ENEMY) ? 48 : 12);   /* 8.4: light frames */
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
        if (timer) { timer--; break; }
        if ((in_pressed & J_A) && B.outcome == OUT_WIN) battle_won();
        else if (in_pressed & J_A) retry_battle();   /* defeat: from the pre-battle snapshot (11.6) */
        break;
    case G_TITLE:
        if ((in_pressed & J_SELECT) && camp_save_ok) load_camp_and_go();
        else if (in_pressed & J_START) {
            if (do_suspend_load() == SAVE_OK) {
                state = G_IDLE; hud_dirty = 1;
                DISPLAY_OFF; mapview_init(&tech01_meta[0][0], tech01_terrain_pal); DISPLAY_ON;
            } else { hud_begin(); hud_tx(0, TX_LOAD_FAIL, 0); hud_end(); start_battle(); }
        } else if (in_pressed & J_A) {
            new_game();
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
    dbg_n_units = B.n_units; dbg_battle = cur_bt;                      /* tests read the 40 B unit records in WRAM bank1 */
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
    VBK_REG = 1; set_bkg_data(HUD_BLANK, 1, blank16); VBK_REG = VBK_TILES;
    rom_set_sprite_data(TECH01_BANK, 0, TECH01_OBJ_TILES, tech01_obj_tiles);
    for (i = 0; i < 20; i++) {
        set_win_tile_xy(i, 0, HUD_BLANK); set_win_tile_xy(i, 1, HUD_BLANK);
        set_win_tile_xy(i, 2, HUD_BLANK); set_win_tile_xy(i, 3, HUD_BLANK);
    }
    WY_REG = 112; WX_REG = 7;
    hud_init(HUD_BLANK);

    {
        uint8_t copy;
        uint32_t seq;
        camp_save_ok = save_probe(SAVE_SLOT_NORMAL0, &copy, &seq) == SAVE_OK && save_kind(SAVE_SLOT_NORMAL0) == SAVE_KIND_CAMP;
    }
    new_game();
    cam_x = 0; cam_y = (uint16_t)(cur_y * 16u + 8u - 56u);
    mapview_init(&tech01_meta[0][0], tech01_terrain_pal);
    {
        uint8_t copy;
        uint32_t seq;
        if (save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK) {
            state = G_TITLE;
            autosave_pending = 0;               /* never overwrite the found save before the choice */
            hud_begin(); hud_tx(0, TX_CONTINUE_Q, 0); hud_tx(1, TX_CONTINUE_KEYS, 0);
            if (camp_save_ok) { hud_space(1); hud_tx(1, TX_TITLE_LOAD, 0); }
            hud_end();
        } else if (camp_save_ok) show_title();
    }

    input_init();
    audio_init();
    audio_ready = 1;
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
