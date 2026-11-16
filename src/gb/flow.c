#pragma bank 14   /* game flow: battle setup, enemy phase, campaign transitions (split from main.c) */
#include "../gen/title_data.h"
#include "../gen/scroll_data.h"
extern uint8_t scroll_sel;
#include <string.h>
#include "game.h"

static void load(uint8_t i, ActiveUnit *u) { unit_load(i, u); }
CampDeploy snap_deploy;                         /* the deployment the snapshot records */

static uint8_t officer_slot(uint8_t o);
static void mark_acted(uint8_t i);
static void enemy_special(const AiCommand *c);
static void enemy_skip_idle(void);
static uint8_t unit_pos(uint8_t i, uint8_t *x, uint8_t *y);
static void enemy_attack(uint8_t target);
static void prewarm_next(void);
static void sync_officers(void);
static void camp_prepare(void);
static void camp_finish(void);
void play_pages(const uint16_t (*pages)[2], uint8_t n) BANKED;
static void go_next(uint8_t next);

/* ---------------------------------------------------------------- save (design doc 11.6) */
uint8_t do_suspend_save(void) BANKED
{
    SaveUi ui;
    uint8_t r;
    ui.battle_id = cur_bt; ui.cam_x = cam_x; ui.cam_y = cam_y; ui.cur_x = cur_x; ui.cur_y = cur_y;
    ui.presentation = 0; ui.text_speed = 0; ui.options = options; ui.gold = gold;
    r = save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
    dbg_save_result = r; dbg_saves++;
    return r;
}

uint8_t do_suspend_load(void) BANKED
{
    SaveUi ui;
    uint8_t r;
    uint16_t id = save_peek_battle(SAVE_SLOT_SUSPEND);
    if (id >= N_BATTLES) return SAVE_ERR_NONE;
    cur_bt = (uint8_t)id;                       /* the map must exist before units and occupancy load */
    unit_base_cache_clear();
    units_clear();
    wram_init_grid_rom(BD->w, BD->h, BD->tbank, BD->terrain);
    r = load_battle(SAVE_SLOT_SUSPEND, &B, &ui);
    if (r != SAVE_OK) return r;
    pending_start = 0; end_fired = 0; DQ->n = 0;
    B.turn_limit = BD->turn_limit;              /* from the battle definition, not the save */
    B.vic_kind = BD->vic_kind; B.vic_unit = BD->vic_unit; B.vic_round = BD->vic_round; B.protect = BD->protect;
    B.vic_x0 = BD->vic_x0; B.vic_y0 = BD->vic_y0; B.vic_x1 = BD->vic_x1; B.vic_y1 = BD->vic_y1;
    cur_bgm = BD->bgm;
    options = ui.options;
    gold = (uint16_t)ui.gold;
    cam_x = ui.cam_x; cam_y = ui.cam_y; cur_x = ui.cur_x; cur_y = ui.cur_y;
    {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH; ai_invalidate(W7_AI); SVBK_REG = o;
    }
    return SAVE_OK;
}

/* unit record i / level i of the current battle: the lists live in BD_UNITS_BANK (6 KiB for 52 battles) */
static Tech01Unit bdu;
static const Tech01Unit *bd_unit(uint8_t i) { rom_copy(BD_UNITS_BANK, &bdu, &BD->units[i], sizeof bdu); return &bdu; }
static uint8_t bd_level(uint8_t i) { uint8_t v; rom_copy(BD_UNITS_BANK, &v, &BD->levels[i], 1); return v; }

/* instance of roster officer o in the current battle: the o-th PLAYER entry of the unit list (2.3) */
static uint8_t officer_slot(uint8_t o)
{
    uint8_t i, k = 0;
    for (i = 0; i < BD->n_units; i++)
        if (bd_unit(i)->faction == FAC_PLAYER) { if (k == o) return i; k++; }
    return 0xFF;
}

/* the battle tables live in this bank: the AI module (bank 12) gets a stack copy */
static void load_ai_groups(void)
{
    uint8_t buf[8 * 5], n = BD->n_ai < 8u ? BD->n_ai : 8u;
    memcpy(buf, BD->ai, (uint16_t)n * 5u);
    ai_groups_init(buf, n);
}

/* 2.4 JOIN: the joiner's record, then its carried items written straight into SRAM (the VM runs deep in
 * the event chain: no second 48 B record on the stack). 0 = no such joiner. */
uint8_t join_officer(uint8_t j) BANKED
{
    const RosterInit *o;
    uint8_t idx, s, v[2];
    if (j >= N_JOINERS) return 0;
    o = &join_defs[j];
    if (roster_find(o->character) != 0xFF) return 1;            /* already in: nothing to do */
    idx = roster_add(o->character, o->cls, o->level, o->str, o->intl, o->cmd);
    if (idx == 0xFF) return 1;                                  /* roster full: the story goes on */
    for (s = 0; s < 8u; s++) {
        v[0] = o->items[s]; v[1] = o->items[s] == 0xFF ? 0xFF : 0;
        sram_write(ROSTER_SRAM_BANK, (uint16_t)((uint16_t)idx * ROSTER_REC + 14u + 2u * s), v, 2);
    }
    return 1;
}

uint16_t bd_title(uint8_t bt) BANKED { return bt < N_BATTLES ? battle_defs[bt].title_tx : 0u; }

/* accessors for code outside bank 14 (vm_gb treasure hook, camp deploy rules) */
uint8_t bd_treasure(uint8_t i, uint8_t *xyi) BANKED
{
    if (i >= BD->n_tre) return 0;
    xyi[0] = BD->tre[i][0]; xyi[1] = BD->tre[i][1]; xyi[2] = BD->tre[i][2];
    return 1;
}

void bd_goal(uint8_t bt, uint16_t *g1, uint16_t *g2) BANKED
{
    *g1 = *g2 = 0;
    if (bt < N_BATTLES) { *g1 = battle_defs[bt].goal_tx; *g2 = battle_defs[bt].goal2_tx; }
}

void bd_deploy_rule(uint8_t bt, uint8_t *max, uint8_t *forced, uint8_t *forbid) BANKED
{
    if (bt >= N_BATTLES) return;
    *max = battle_defs[bt].deploy_max; *forced = battle_defs[bt].forced; *forbid = battle_defs[bt].forbid;
}

void start_battle(void) BANKED
{
    uint8_t i;
    cur_bgm = BD->bgm;                         /* every battle starts its own music (also after a camp / retry) */
    if (audio_ready && state != G_TITLE) music_play_id(cur_bgm);
    danger_hide();
    memset(&B, 0, sizeof B);
    rng_seed(&B.rng, BD->seed);
    /* 2.4: round_mode CARRY continues the previous battle's round (turn_limit is absolute) */
    B.round = (BD->round_mode == ROUND_CARRY && carry_round) ? carry_round : 1u;
    B.turn_limit = BD->turn_limit; B.leader = 0;
    B.vic_kind = BD->vic_kind; B.vic_unit = BD->vic_unit; B.vic_round = BD->vic_round; B.protect = BD->protect;
    B.vic_x0 = BD->vic_x0; B.vic_y0 = BD->vic_y0; B.vic_x1 = BD->vic_x1; B.vic_y1 = BD->vic_y1;   /* 6.6 */
    units_clear();
    wram_init_grid_rom(BD->w, BD->h, BD->tbank, BD->terrain);
    vm_clear_battle_flags();                   /* once bits + battle-scope flags are per battle */
    DQ->n = 0;
    end_fired = 0;
    {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH;
        ai_invalidate(W7_AI);
        SVBK_REG = o;
    }
    inv_clear_all();                           /* officers bring their items from the roster (5.6) */
    load_ai_groups();                          /* 8.2: from the battle definition */
    {   /* 2.3: deployed officers fill the slots in order; slot k = cell of the k-th player entry */
        uint8_t k, n = deploy.n;
        if (!n) for (k = 0; k < roster_count() && n < BD->deploy_max; k++) if (roster_available(k)) deploy.order[n++] = k;   /* default */
        for (k = 0; k < n; k++) {
            uint8_t o = deploy.order[k], inst = officer_slot(o), sk = officer_slot(k), fl;
            if (inst == 0xFF || sk == 0xFF || !roster_available(o)) continue;   /* left the party (2.4) */
            sram_read(ROSTER_SRAM_BANK, (uint16_t)((uint16_t)o * ROSTER_REC + 45u), &fl, 1);   /* reserved[0] */
            if (BD->continuity == CONT_NO_REPLENISH && (fl & ROSTER_RETREATED)) continue;   /* 2.4 */
            {
                const Tech01Unit *cell = bd_unit(sk);
                roster_deploy(&B, o, inst, cell->x, cell->y, BD->continuity == CONT_NORMAL);
            }
        }
        /* 11.6 pre-battle snapshot: written from the main loop before BATTLE_START (write_snapshot) */
        snap_deploy.n = n;
        for (k = 0; k < n; k++) snap_deploy.order[k] = deploy.order[k];
        deploy.n = 0;                          /* the next camp starts from everyone again */
    }
    for (i = 0; i < BD->n_units; i++) {
        const Tech01Unit *t = bd_unit(i);
        ActiveUnit u;
        if (t->faction == FAC_PLAYER) continue;
        battle_spawn(&B, i, t->ch ? (uint16_t)t->ch : ANON_CHARACTER, t->cls, t->faction, bd_level(i), t->x, t->y);
        load(i, &u); u.ai_id = t->ai; unit_store(i, &u);
    }
    for (i = 0; i < BD->n_items; i++) { inv_add(BD->items[i][0], BD->items[i][1]); item_rederive(BD->items[i][0]); }
    if (B.n_units < BD->n_units) B.n_units = BD->n_units;   /* undeployed slots stay empty records */
    publish_inv();
    publish_treasures();
    prize_clear();                             /* the queue is per battle; the camp empties it first */

    {
        uint8_t s0 = officer_slot(0);
        cur_x = bd_unit(s0)->x; cur_y = bd_unit(s0)->y;
    }
    battle_phase_start(&B, FAC_PLAYER);
    state = G_BANNER; timer = 45; next_state = G_IDLE;
    hud_banner(FAC_PLAYER);
    autosave_pending = (uint8_t)(options & OPT_AUTOSAVE);
    pending_start = 1;                         /* BATTLE_START + PHASE_START fire once the LCD is on */
}

void new_game(void) BANKED
{
    uint8_t k, s;
    RosterRec r;
    roster_clear();
    unit_base_cache_clear();
    for (k = 0; k < N_ROSTER_INIT; k++) {
        const RosterInit *o = &roster_init[k];
        uint8_t idx = roster_add(o->character, o->cls, o->level, o->str, o->intl, o->cmd);
        roster_read(idx, &r);
        for (s = 0; s < 8; s++) { r.items[2 * s] = o->items[s]; r.items[2 * s + 1] = o->items[s] == 0xFF ? 0xFF : 0; }
        roster_write(idx, &r);
    }
    {   /* campaign area (global flags) starts clear */
        uint8_t z[32], off;
        memset(z, 0, sizeof z);
        for (off = 0; off < 224u; off += 32u) campaign_write(off, z, 32);
        campaign_write(224u, z, 32);
    }
    gold = 0; cur_bt = 0; carry_round = 0; deploy.n = 0;
    start_battle();
}

uint8_t player_can_act(void) BANKED
{
    uint8_t i;
    ActiveUnit u;
    for (i = 0; i < B.n_units; i++) {
        load(i, &u);
        if (u.faction == FAC_PLAYER && battle_can_act(&u)) return 1;
    }
    return 0;
}

static uint8_t ai_fac;                       /* faction whose AI phase is running (ALLY / ENEMY) */


/* first unit of a faction that can act this phase, or 0xFF (own frame: keeps the record off the chain) */
static uint8_t first_actor(uint8_t fac)
{
    uint8_t i;
    ActiveUnit u;
    for (i = 0; i < B.n_units; i++) { load(i, &u); if (u.faction == fac && battle_can_act(&u)) return i; }
    return 0xFF;
}

/* 4.1 phase order PLAYER -> ALLY -> ENEMY: an AI phase (the ally phase is skipped without allies).
 * No recursion and no unit record here: PHASE_START scripts (spawns) run below this frame. */
static void start_ai_phase(uint8_t fac)
{
    uint8_t o, i, first;
    for (;;) {
        battle_phase_start(&B, fac);
        fire(TR_PHASE_START, fac, 0xFF, 0xFF, 0xFF);
        if (B.outcome) return;
        first = first_actor(fac);
        if (fac == FAC_ALLY && first == 0xFF) {
            battle_phase_end(&B);
            if (B.outcome) return;
            fac = FAC_ENEMY;
            continue;
        }
        break;
    }
    ai_fac = fac;
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    for (i = 0; i < B.n_units; i++) ai_hold_update(&B, W7_MR, i);   /* 8.2 HOLD alert, once per phase */
    ai_invalidate(W7_AI);
    if (first != 0xFF) ai_field_prepare(&B, W7_AI, first);           /* pre-warm the first actor's field */
    SVBK_REG = o;
    enemy_i = 0;
    state = G_BANNER; timer = 45; next_state = G_ENEMY;
    hud_banner(fac);
}

void begin_enemy_phase(void) BANKED
{
    danger_hide();
    battle_phase_end(&B);                      /* PLAYER end */
    if (B.outcome) return;
    start_ai_phase(FAC_ALLY);
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
        if (u.faction == ai_fac && battle_can_act(&u)) break;
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
        if (u.faction == ai_fac && battle_can_act(&u)) break;
    }
    if (j >= B.n_units) return;
    (void)m;
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    ai_field_prepare(&B, W7_AI, j);            /* move type and target-specific source (8.4) */
    SVBK_REG = o;
}

void enemy_step(void) BANKED
{
    AiCommand c;
    uint8_t o, ux, uy;
    uint16_t t0;
    enemy_skip_idle();
    if (enemy_i >= B.n_units && ai_fac == FAC_ALLY) {   /* allies done: the enemy phase follows */
        battle_phase_end(&B);
        if (B.outcome) return;
        start_ai_phase(FAC_ENEMY);
        return;
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
    unit_pos(enemy_i, &ux, &uy);
    ai_resolve(enemy_i, &ai_mode);             /* 8.2: group / instance override (SRAM12) */
    o = SVBK_REG;
    t0 = sys_time;
    SVBK_REG = WB_SCRATCH;
    ai_decide_mode(&B, W7_AI, W7_MR, enemy_i, &ai_mode, &c);
    SVBK_REG = o;
    dbg_t_ai = sys_time - t0;
    if (c.x != ux || c.y != uy) {
        battle_move(enemy_i, c.x, c.y);
        fire(TR_POST_MOVE, ai_fac, enemy_i, 0xFF, 0xFF);
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

/* ---------------------------------------------------------------- campaign flow (2.1, 2.4, 5.6) */
static void sync_officers(void)
{
    uint8_t o, inst;
    ActiveUnit u;
    for (o = 0; o < roster_count(); o++) {
        inst = officer_slot(o);
        if (inst == 0xFF) continue;
        load(inst, &u);
        if (u.max_hp && u.roster_offset == (uint16_t)o * ROSTER_REC) roster_sync(o, inst);   /* deployed */
    }
}

/* the camp works on the officers as instances 0..n-1 (stats + item blocks), synced back afterwards */
static void camp_prepare(void)
{
    uint8_t o;
    units_clear();
    inv_clear_all();
    for (o = 0; o < roster_count() && o < CAMP_MAX_OFF; o++) roster_deploy(&B, o, o, 0, 0, 0);
}

static void camp_finish(void)
{
    uint8_t o;
    for (o = 0; o < roster_count() && o < CAMP_MAX_OFF; o++) roster_sync(o, o);
}

void play_pages(const uint16_t (*pages)[2], uint8_t n) BANKED
{
    uint8_t i;
    for (i = 0; i < n && i < DQ_MAX; i++) { DQ->speaker[i] = (uint8_t)pages[i][0]; DQ->text[i] = pages[i][1]; }
    DQ->n = i;
    play_queue();
}

/* the way on from the battle just won: the alt edge when its global flag is set (2.5 branch at 麦城) */
static void route(uint8_t *camp, uint8_t *next)
{
    *camp = BD->camp; *next = BD->next;
    if (BD->alt_flag != 0xFF && vm_flag_get(SCOPE_GLOBAL, BD->alt_flag)) { *camp = BD->alt_camp; *next = BD->alt_next; }
}

static void go_next(uint8_t next)
{
    if (next == 0xFF) {                        /* 2.5: an ending; which one is the last battle's */
        uint8_t e = BD->ending < N_ENDINGS ? BD->ending : 0u;
        music_play_id(e == 0 ? MUS_SAD : MUS_END);
        scroll_sel = (uint8_t)(SCROLL_END_A + e);   /* the ending's own landscape */
        play_pages(ending_pages[e], ending_len[e]);
        scroll_sel = 0xFF;
        new_game();
        show_title();
        return;
    }
    cur_bt = next;
    start_battle();                            /* plays the battle's own music */
    map_mode_restore();
}

void battle_won(void) BANKED
{
    uint8_t camp, next;
    route(&camp, &next);
    sync_officers();                           /* 5.6 before anything else changes */
    carry_round = B.round;
    if (camp != 0xFF || prize_count()) {
        music_play(&song_camp);                /* 14.4: camp theme */
        camp_prepare();
        camp_run(&gold, &deploy, camp == 0xFF ? CAMP_PRIZES_ONLY : CAMP_FULL, next, camp);   /* LCD off on return */
        camp_finish();
    } else DISPLAY_OFF;
    go_next(next);
}

void load_camp_and_go(void) BANKED
{
    SaveUi ui;
    if (load_camp(SAVE_SLOT_NORMAL0, &ui) != SAVE_OK || ui.battle_id >= N_BATTLES) {
        hud_begin(); hud_tx(0, TX_LOAD_FAIL, 0); hud_end();
        return;
    }
    gold = (uint16_t)ui.gold; options = ui.options;
    camp_council_done = ui.presentation & 1u;  /* T-SAVE-01: 军议已毕 comes back with the save */
    unit_base_cache_clear();                   /* a different roster may have been loaded */
    cur_bt = (uint8_t)ui.battle_id; carry_round = 0; deploy.n = 0;
    DISPLAY_OFF;
    music_play(&song_camp);
    camp_prepare();
    {
        uint8_t camp, next;
        route(&camp, &next);
        camp_run(&gold, &deploy, CAMP_FULL, next, camp == 0xFF ? 0u : camp);
        camp_finish();
        go_next(next);
    }
}

/* T-FLOW-01: the battle (with a camp) that leads into the current one, or 0xFF */
uint8_t prev_camp_battle(void) BANKED
{
    uint8_t p;
    for (p = 0; p < N_BATTLES; p++)
        if ((battle_defs[p].next == cur_bt && battle_defs[p].camp != 0xFF)
            || (battle_defs[p].alt_flag != 0xFF && battle_defs[p].alt_next == cur_bt && battle_defs[p].alt_camp != 0xFF)) return p;
    return 0xFF;
}

/* T-FLOW-01 defeat -> back to the camp before this battle: the pre-battle snapshot comes back first (roster,
 * flags, gold as they were when the army set out), then the camp opens with 军议 already held. Setting out
 * again writes a new snapshot, so purchases made now count from then on. */
void retry_to_camp(void) BANKED
{
    Battle sb;
    SaveUi ui;
    uint8_t dep[1 + 4 * SAVE_MAX_DEPLOY], k, target = cur_bt, p;
    DISPLAY_OFF;
    if (load_snapshot(&sb, &ui, dep) == SAVE_OK && ui.battle_id < N_BATTLES) {
        target = (uint8_t)ui.battle_id; gold = (uint16_t)ui.gold;
        deploy.n = dep[0] > CAMP_MAX_DEPLOY ? CAMP_MAX_DEPLOY : dep[0];
        for (k = 0; k < deploy.n; k++) deploy.order[k] = dep[1 + 4 * k];
        unit_base_cache_clear();
        dbg_retries++;
    }
    cur_bt = target;
    p = prev_camp_battle();
    if (p == 0xFF) { start_battle(); mapview_init(&tech01_meta[0][0], tech01_terrain_pal); map_mode_restore(); return; }
    cur_bt = p; carry_round = 0;
    prize_clear();                             /* the lost battle's unclaimed spoils are gone with it */
    camp_council_done = 1;
    music_play(&song_camp);
    camp_prepare();
    camp_run(&gold, &deploy, CAMP_FULL, target, BD->next == target ? BD->camp : BD->alt_camp);
    camp_finish();
    go_next(target);
}

/* 11.6 retry: restore the pre-battle snapshot (roster, flags, gold, deployment) and start again.
 * Own frame (bank 14): its buffers must not sit in logic()'s frame above every deep call chain. */
void retry_battle(void) BANKED
{
    Battle sb;
    SaveUi ui;
    uint8_t dep[1 + 4 * 16], k;
    DISPLAY_OFF;
    if (load_snapshot(&sb, &ui, dep) == SAVE_OK && ui.battle_id < N_BATTLES) {
        cur_bt = (uint8_t)ui.battle_id; gold = (uint16_t)ui.gold;
        carry_round = sb.round;
        deploy.n = dep[0] > CAMP_MAX_DEPLOY ? CAMP_MAX_DEPLOY : dep[0];
        for (k = 0; k < deploy.n; k++) deploy.order[k] = dep[1 + 4 * k];
        unit_base_cache_clear();
        dbg_retries++;
    }
    start_battle();
    map_mode_restore();                        /* the result page replaced the map's tiles and palettes */
}

/* 11.6 pre-battle snapshot (SRAM13/14). Called from logic() just before BATTLE_START fires, at the top
 * of the call stack: from inside start_battle the save writer's frames overflowed the 512 B stack. */
typedef char dep_fits[(CAMP_MAX_DEPLOY == SAVE_MAX_DEPLOY) ? 1 : -1];
void write_snapshot(void) BANKED
{
    SaveUi ui;
    uint8_t dep[1 + 4 * CAMP_MAX_DEPLOY], k;
    dep[0] = snap_deploy.n;
    for (k = 0; k < snap_deploy.n; k++) { dep[1 + 4 * k] = snap_deploy.order[k]; dep[2 + 4 * k] = 0; dep[3 + 4 * k] = 0; dep[4 + 4 * k] = 0; }
    memset(&ui, 0, sizeof ui);
    ui.battle_id = cur_bt; ui.gold = gold; ui.options = options;
    dbg_snapshot = save_snapshot(&B, &ui, dep);
}
