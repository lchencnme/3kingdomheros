/* Items, inventory, exchange (design doc 7.1-7.3) and AI strategy/item candidates (8.3). */
#include <stdio.h>
#include <string.h>
#include "../../src/core/item.h"
#include "../../src/core/ai.h"
#include "../../src/core/strategy.h"
#include "../../src/core/save.h"
#include "../../src/core/grid.h"
#include "../../src/core/rules.h"
#include "../../src/core/vm.h"
#include "../../src/core/roster.h"
#include "../../src/core/undo.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];
extern uint8_t host_sram[16][8192];
extern TreasureDef host_treasures[8];
extern uint8_t host_n_treasures;
extern uint8_t host_campaign[256];

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Battle B;
static AiScratch S;
static MoveRange MR;
static ActiveUnit tmpu;

static void reset(void)
{
    uint8_t i;
    grid_w = 12; grid_h = 12;
    memset(host_terrain, 0, sizeof host_terrain); memset(host_occ, 0, sizeof host_occ);
    memset(host_units, 0, sizeof host_units);
    memset(host_sram, 0, sizeof host_sram);
    for (i = 0; i < MAX_UNITS; i++) { host_base[i].str = 40; host_base[i].intl = 90; host_base[i].cmd = 60; }
    memset(&B, 0, sizeof B); rng_seed(&B.rng, 7); B.round = 1; B.turn_limit = 30;
    inv_clear_all();
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 5, 2, 5);           /* infantry */
    battle_spawn(&B, 1, 2, 0, FAC_PLAYER, 5, 3, 5);           /* adjacent ally */
    battle_spawn(&B, 2, 0xFFFF, 0, FAC_ENEMY, 1, 4, 5);       /* enemy 2 cells east of unit 0 */
    ai_invalidate(&S);
}

int main(void)
{
    ItemResult r;
    uint32_t d0;
    uint16_t hp;
    uint8_t i;

    /* inventory: FFFF = empty, fill order, capacity 8 */
    reset();
    CHECK(inv_get(0, 0) == ITEM_NONE && inv_count(0) == 0, "cleared inventory is empty");
    CHECK(host_sram[INV_SRAM_BANK][0] == 0xFF && host_sram[INV_SRAM_BANK][1] == 0xFF, "empty slot stored as FFFF");
    for (i = 0; i < INV_SLOTS; i++) CHECK(inv_add(0, 0) == i, "add to slot %u", i);
    CHECK(inv_add(0, 1) == ITEM_NONE && inv_count(0) == 8, "9th item refused, nothing swallowed");
    CHECK(host_sram[INV_SRAM_BANK][2 * 7] == 0 && host_sram[INV_SRAM_BANK][2 * 7 + 1] == 0, "slot 7 = item 0x0000 LE");

    /* HP item on adjacent ally: restores min(power, deficit), consumes item + action, 0 draws */
    reset();
    inv_set(0, 3, 0);                                           /* 干粮 +100 */
    host_units[1].hp = (uint16_t)(host_units[1].max_hp - 60);
    d0 = B.rng.draws;
    CHECK(item_check(&B, 0, 3, 3, 5) == IR_OK, "heal adjacent ally ok");
    CHECK(item_use(&B, 0, 3, 3, 5, &r) == IR_OK && r.amount == 60, "heal clamps to deficit (%u)", r.amount);
    CHECK(host_units[1].hp == host_units[1].max_hp, "ally at full hp");
    CHECK(inv_get(0, 3) == ITEM_NONE, "item consumed");
    CHECK(host_units[0].action_flags & AF_ACTED, "user's action consumed");
    CHECK(B.rng.draws == d0, "items draw no RNG");

    /* not applicable: full target, out of range, hostile target, empty slot -> not consumed */
    reset();
    inv_set(0, 0, 0);
    CHECK(item_check(&B, 0, 0, 3, 5) == IR_NO_EFFECT, "full hp: not usable");
    CHECK(item_use(&B, 0, 0, 3, 5, &r) == IR_NO_EFFECT && inv_get(0, 0) == 0, "not consumed on failure");
    host_units[2].hp = 10;
    CHECK(item_check(&B, 0, 0, 4, 5) == IR_RANGE, "range 1");
    CHECK(item_check(&B, 1, 0, 4, 5) == IR_EMPTY, "other unit's slot is empty");
    inv_set(1, 0, 0);
    CHECK(item_check(&B, 1, 0, 4, 5) == IR_NO_TARGET, "hp item never on an enemy");
    host_units[0].hp = 5;
    CHECK(item_check(&B, 0, 0, 2, 5) == IR_OK, "self use allowed");

    /* morale / mp / cleanse */
    reset();
    inv_set(0, 0, 4); host_units[0].morale = 80;
    CHECK(item_use(&B, 0, 0, 2, 5, &r) == IR_OK && host_units[0].morale == 100 && r.amount == 20, "morale clamps at 100");
    reset();
    inv_set(0, 0, 7);
    CHECK(item_check(&B, 0, 0, 3, 5) == IR_NO_EFFECT, "cleanse needs a status");
    host_units[1].status_mask = (1u << ST_POISON) | (1u << ST_ROOT); host_units[1].ttl[ST_POISON] = 3;
    CHECK(item_use(&B, 0, 0, 3, 5, &r) == IR_OK && host_units[1].status_mask == 0 && host_units[1].ttl[ST_POISON] == 0, "cleanse clears all");

    /* fire pot: fixed 120, range 2, always hits, no counter, may defeat */
    reset();
    inv_set(0, 0, 8);
    hp = host_units[2].hp;
    d0 = B.rng.draws;
    CHECK(item_check(&B, 0, 0, 3, 5) == IR_NO_TARGET, "fire pot never on an ally");
    CHECK(item_use(&B, 0, 0, 4, 5, &r) == IR_OK, "fire pot at range 2");
    CHECK(r.amount == (hp < 120 ? hp : 120) && host_units[2].hp == hp - r.amount, "fixed damage %u", r.amount);
    CHECK(B.rng.draws == d0 && !(host_units[2].action_flags & AF_COUNTER), "0 draws, no counter");
    reset();
    inv_set(0, 0, 8); host_units[2].hp = 50;
    CHECK(item_use(&B, 0, 0, 4, 5, &r) == IR_OK && r.killed && (host_units[2].action_flags & AF_RETREAT), "fire pot defeats");
    CHECK(B.outcome == OUT_WIN, "last enemy removed -> win");

    /* weather charms: self only, not when already in that weather */
    reset();
    inv_set(0, 0, 9); inv_set(0, 1, 10);
    CHECK(item_check(&B, 0, 1, 2, 5) == IR_NO_EFFECT, "止雨符 in clear weather: no effect");
    CHECK(item_check(&B, 0, 0, 3, 5) == IR_RANGE, "charm is self only");
    CHECK(item_use(&B, 0, 0, 2, 5, &r) == IR_OK && B.weather == 1, "祈雨符 -> rain");
    host_units[0].action_flags = 0;
    CHECK(item_use(&B, 0, 1, 2, 5, &r) == IR_OK && B.weather == 0, "止雨符 -> clear");

    /* battle exchange: adjacent allies, swap slots, actor's action only */
    reset();
    inv_set(0, 2, 1); inv_set(1, 5, 3);
    CHECK(item_exchange(0, 2, 2, 0) == IR_NO_TARGET, "never with an enemy");
    CHECK(item_exchange(0, 2, 1, 5) == IR_OK && inv_get(0, 2) == 3 && inv_get(1, 5) == 1, "swap");
    CHECK((host_units[0].action_flags & AF_ACTED) && !(host_units[1].action_flags & AF_ACTED), "actor acted, receiver did not");
    reset();
    inv_set(0, 0, 1);
    CHECK(item_exchange(0, 0, 1, 7) == IR_OK && inv_get(1, 7) == 1 && inv_get(0, 0) == ITEM_NONE, "give into an empty slot");
    CHECK(item_exchange(0, 1, 1, 6) == IR_EMPTY, "two empty slots: nothing to exchange");
    host_units[1].x = 6; host_units[1].action_flags = 0; host_units[0].action_flags = 0;
    CHECK(item_exchange(1, 7, 0, 0) == IR_RANGE, "not adjacent");

    /* AI: a damaged enemy with rations heals itself rather than wait */
    reset();
    {
        AiCommand c;
        host_units[2].x = 10; host_units[2].y = 10; host_occ[5 * 12 + 4] = 0; host_occ[10 * 12 + 10] = 2;
        host_units[2].hp = 20;
        inv_set(2, 4, 1);                                       /* 军粮 +300 */
        ai_decide(&B, &S, &MR, 2, AI_AGGRESSIVE, &c);
        CHECK(c.action == ACT_ITEM && c.param == 4, "AI uses its ration (action %u)", c.action);
        battle_move(2, c.x, c.y);
        CHECK(item_use(&B, 2, c.param, c.tx, c.ty, &r) == IR_OK && host_units[2].hp == host_units[2].max_hp, "command is executable (hp %u)", host_units[2].hp);
    }

    /* AI: an enemy strategist casts fire on a target out of normal reach */
    reset();
    {
        AiCommand c;
        StratEstimate se;
        battle_spawn(&B, 3, 0xFFFF, 9, FAC_ENEMY, 5, 9, 5);     /* enemy 文士 */
        host_units[2].x = 11; host_units[2].y = 11; host_occ[5 * 12 + 4] = 0; host_occ[11 * 12 + 11] = 2;
        CHECK(strategy_estimate(&B, &host_units[3], 3, 0, 3, 5, &se, &tmpu) && se.dmg > 0, "estimate fire on unit 1 (%u)", se.dmg);
        CHECK(!strategy_estimate(&B, &host_units[3], 3, 0, 9, 5, &se, &tmpu), "no estimate on own cell");
        ai_decide(&B, &S, &MR, 3, AI_AGGRESSIVE, &c);
        CHECK(c.action == ACT_STRATEGY, "AI chooses a strategy (action %u)", c.action);
        if (c.action == ACT_STRATEGY) {
            StrategyResult sr;
            uint8_t ox = host_units[3].x, oy = host_units[3].y;
            battle_move(3, c.x, c.y);
            CHECK(strategy_cast(&B, 3, c.param, c.tx, c.ty, &sr) == SR_OK, "AI strategy command is legal (from %u,%u to %u,%u)", ox, oy, c.x, c.y);
        }
        /* rain: fire impossible, no strategy candidate from fire */
        B.weather = 1;
        host_units[3].action_flags = 0;
        ai_invalidate(&S);
        ai_decide(&B, &S, &MR, 3, AI_AGGRESSIVE, &c);
        CHECK(!(c.action == ACT_STRATEGY && strategy_defs[c.param].cond == SC_FIRE), "no fire in rain");
    }

    /* equipment (7.1): bonuses through every derivation, one per category, civilians cannot equip */
    reset();
    {
        uint16_t atk0 = host_units[0].atk, agi0 = host_units[0].agility, mv0 = host_units[0].move, mag0 = host_units[0].mag;
        inv_set(0, 0, 0x41); inv_set(0, 1, 0x43); inv_set(0, 2, 0x61); inv_set(0, 3, 0x50);
        CHECK(item_category(0x41) == EQ_WEAPON && item_category(0x61) == EQ_MOUNT && item_category(0) == EQ_NONE, "categories");
        CHECK(item_equip(0, 0) == IR_OK && host_units[0].atk == atk0 + 12, "青龙偃月刀 ATK+12 (%u)", host_units[0].atk);
        CHECK(item_equip(0, 1) == IR_OK && host_units[0].atk == atk0 + 10 && host_units[0].agility == agi0 + 5 && inv_equipped(0, EQ_WEAPON) == 1,
              "one weapon at a time: 青釭剑 replaces it");
        CHECK(item_equip(0, 2) == IR_OK && host_units[0].move == mv0 + 1 && host_units[0].agility == agi0 + 15, "赤兔 move+1 AGI+10");
        CHECK(item_equip(0, 3) == IR_OK && host_units[0].mag == mag0 + 10, "孙子兵法 MAG+10");
        CHECK(item_equip(0, 4) == IR_EMPTY && item_equip(0, 5) == IR_EMPTY, "empty slot");
        inv_set(0, 4, 0);
        CHECK(item_equip(0, 4) == IR_NO_EFFECT, "rations are not equipment");
        host_units[0].hp = 100;
        hp = host_units[0].max_hp;
        item_unequip(0, EQ_WEAPON);
        CHECK(host_units[0].atk == atk0 && host_units[0].hp == 100 && host_units[0].max_hp == hp, "unequip; hp deficit kept");
        inv_set(0, 2, ITEM_NONE);
        item_rederive(0);
        CHECK(host_units[0].move == mv0 && inv_equipped(0, EQ_MOUNT) == ITEM_NONE, "removing the item drops the equip mark");
        battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 5, 2, 5);
        CHECK(host_units[0].mag == mag0 + 10, "spawn derivation includes equipment");
        battle_spawn(&B, 5, 0xFFFF, 17, FAC_PLAYER, 1, 8, 8);    /* 民众 */
        inv_set(5, 0, 0x47);
        CHECK(item_equip(5, 0) == IR_CLASS, "civilian cannot equip");
        CHECK(item_sell_price(0x47) == 250 && item_sell_price(0x41) == 0 && item_sell_price(0x70) == 0 && item_sell_price(1) == 75,
              "sell = floor(price/2), unsold/plot items cannot be sold");
    }
    /* battle exchange never moves equipped items; camp exchange unequips and re-derives */
    reset();
    inv_set(0, 0, 0x47); item_equip(0, 0);
    CHECK(item_exchange(0, 0, 1, 0) == IR_EQUIPPED && inv_get(0, 0) == 0x47, "equipped item stays in battle");
    {
        uint16_t atk = host_units[0].atk;
        CHECK(item_exchange_camp(0, 0, 2, 3) == IR_OK && inv_get(2, 3) == 0x47 && host_units[0].atk == atk - 6
              && inv_equipped(0, EQ_WEAPON) == ITEM_NONE, "camp exchange: any distance, unequips, re-derives");
    }

    /* strategy book: no MP, any class, MAG 60, 2 draws, no XP, consumed even on a miss */
    reset();
    {
        StrategyResult sr;
        uint16_t mp0 = host_units[0].mp;
        uint8_t xp0 = host_units[0].exp;
        inv_set(0, 0, 0x10);                                  /* 火攻书 */
        CHECK(!strategy_known(&host_units[0], 0), "infantry does not know 火攻");
        CHECK(item_check(&B, 0, 0, 4, 5) == IR_OK, "book usable by infantry at range 2");
        host_units[0].status_mask = 1u << ST_SILENCE; host_units[0].ttl[ST_SILENCE] = 2;
        CHECK(item_check(&B, 0, 0, 4, 5) == IR_OK, "books ignore SILENCE");
        d0 = B.rng.draws; hp = host_units[2].hp;
        CHECK(item_use_book(&B, 0, 0, 4, 5, &sr) == IR_OK && sr.n == 1, "book cast");
        CHECK(B.rng.draws - d0 == 2 && host_units[0].mp == mp0 && host_units[0].exp == xp0 && sr.xp == 0, "2 draws, no MP, no XP");
        if (sr.hit[0]) {
            uint16_t full = rules_mag_damage(20, ITEM_SCROLL_MAG, host_units[2].res, 100, 100, 90, 0);
            CHECK(sr.amount[0] >= full * 9 / 10 - 1 && hp - host_units[2].hp == sr.amount[0], "book damage uses MAG 60 (%u)", sr.amount[0]);
        }
        CHECK(inv_get(0, 0) == ITEM_NONE && (host_units[0].action_flags & AF_ACTED), "book consumed, action used");
        B.weather = 1; inv_set(1, 0, 0x10);
        CHECK(item_check(&B, 1, 0, 4, 5) == IR_NO_EFFECT || item_check(&B, 1, 0, 4, 5) == IR_RANGE, "fire book still checks weather/range");
    }
    /* promotion / class change (5.5) */
    reset();
    inv_set(0, 0, 0x30); inv_set(0, 1, 0x39);
    CHECK(item_check(&B, 0, 0, 2, 5) == IR_CLASS, "长戟 needs L12");
    host_units[0].level = 12; item_rederive(0);
    host_units[0].hp = (uint16_t)(host_units[0].max_hp - 30);
    CHECK(item_use(&B, 0, 0, 2, 5, &r) == IR_OK && host_units[0].class_id == 1 && host_units[0].level == 12, "0 -> 1 at L12");
    CHECK(host_units[0].hp == host_units[0].max_hp - 30, "promotion keeps the deficit");
    host_units[0].action_flags = 0;
    CHECK(item_use(&B, 0, 1, 2, 5, &r) == IR_OK && host_units[0].class_id == 3, "弓术指南 -> 弓手");
    inv_set(0, 2, 0x39); host_units[0].action_flags = 0;
    CHECK(item_check(&B, 0, 2, 2, 5) == IR_CLASS, "already 弓手");
    /* 青囊书 heal +20% */
    reset();
    {
        StrategyResult sr;
        uint16_t base;
        battle_spawn(&B, 6, 9, 9, FAC_PLAYER, 5, 2, 6);       /* 文士 below unit 0 */
        host_units[0].hp = 10;
        base = (uint16_t)(strategy_defs[9].power + host_units[6].mag / 2u);
        inv_set(6, 0, 0x55); item_equip(6, 0);
        CHECK(strategy_cast(&B, 6, 9, 2, 5, &sr) == SR_OK && sr.amount[0] == base * 120u / 100u, "青囊书 heal %u vs %u", sr.amount[0], base);
    }

    /* ---- AI modes (8.2) */
    {
        static const uint8_t groups[] = {0, AI_AGGRESSIVE, 0xFF, 0xFF, 3,  1, AI_GUARD_POINT, 8 * 12 + 8, 0, 2};
        AiMode m;
        reset();
        ai_groups_init(groups, 2);
        host_units[2].ai_id = 1;
        ai_resolve(2, &m);
        CHECK(m.mode == AI_GUARD_POINT && m.target == 104 && m.leash == 2, "group record");
        ai_resolve(1, &m);
        CHECK(m.mode == AI_AGGRESSIVE && m.target == AI_NO_TARGET, "group 0");
        ai_set(2, AI_ESCAPE, 11, 0);
        ai_resolve(2, &m);
        CHECK(m.mode == AI_ESCAPE && (m.flags & AIF_OVERRIDE), "instance override wins");
        ai_set(0x8001u, AI_HOLD, AI_NO_TARGET, 3);
        ai_resolve(2, &m);
        CHECK(m.mode == AI_ESCAPE, "changing the group keeps the override");
        CHECK(host_sram[AI_SRAM_BANK][AI_SRAM_GROUPS + 5 * 5] == AI_HOLD, "undefined groups default to HOLD");
    }
    /* GUARD_POINT: never ends beyond the leash */
    reset();
    {
        AiMode m;
        AiCommand c;
        host_units[2].x = 10; host_units[2].y = 10; host_occ[5 * 12 + 4] = 0; host_occ[10 * 12 + 10] = 2;
        m.mode = AI_GUARD_POINT; m.target = 10 * 12 + 10; m.leash = 1; m.flags = 0;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK((c.x > 10 ? c.x - 10 : 10 - c.x) + (c.y > 10 ? c.y - 10 : 10 - c.y) <= 1, "guard stays within leash 1 (%u,%u)", c.x, c.y);
        m.mode = AI_AGGRESSIVE; m.target = AI_NO_TARGET;
        host_units[2].action_flags = 0;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK((c.x > 10 ? c.x - 10 : 10 - c.x) + (c.y > 10 ? c.y - 10 : 10 - c.y) > 1, "same unit AGGRESSIVE moves further (%u,%u)", c.x, c.y);
    }
    /* ESCORT: stays near the escortee */
    reset();
    {
        AiMode m; AiCommand c;
        battle_spawn(&B, 3, 0xFFFF, 0, FAC_ENEMY, 1, 9, 9);
        host_units[2].x = 10; host_units[2].y = 10; host_occ[5 * 12 + 4] = 0; host_occ[10 * 12 + 10] = 2;
        m.mode = AI_ESCORT; m.target = 3; m.leash = 2; m.flags = 0;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK((c.x > 9 ? c.x - 9 : 9 - c.x) + (c.y > 9 ? c.y - 9 : 9 - c.y) <= 2, "escort within 2 of unit 3 (%u,%u)", c.x, c.y);
    }
    /* ESCAPE: heads for the exit and takes it when reachable */
    reset();
    {
        AiMode m; AiCommand c;
        host_units[2].x = 9; host_units[2].y = 10; host_occ[5 * 12 + 4] = 0; host_occ[10 * 12 + 9] = 2;
        m.mode = AI_ESCAPE; m.target = 11 * 12 + 11; m.leash = 0; m.flags = 0;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK(c.x == 11 && c.y == 11, "escape reaches the exit (%u,%u)", c.x, c.y);
        m.target = 0 * 12 + 11;  host_units[2].action_flags = 0;
        host_units[2].x = 8; host_units[2].y = 11; host_occ[10 * 12 + 9] = 0; host_occ[11 * 12 + 8] = 2;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK(c.y < 11, "escape moves toward a far exit (%u,%u)", c.x, c.y);
    }
    /* HOLD: stays until threatened (exact reach + range), then stays alerted */
    reset();
    {
        static const uint8_t g[] = {0, AI_HOLD, 0xFF, 0xFF, 3};
        AiMode m; AiCommand c;
        ai_groups_init(g, 1);
        host_units[2].x = 11; host_units[2].y = 0; host_occ[5 * 12 + 4] = 0; host_occ[0 * 12 + 11] = 2;
        CHECK(ai_hold_update(&B, &MR, 2) == 0, "phase-start check: safe");
        ai_resolve(2, &m);
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK(c.x == 11 && c.y == 0 && c.action == ACT_WAIT, "HOLD: far opponents -> stays");
        ai_resolve(2, &m);
        CHECK(!(m.flags & AIF_ALERT), "not alerted");
        battle_move(1, 9, 1);                                   /* infantry can step next to it and attack */
        host_units[2].action_flags = 0;
        CHECK(ai_hold_update(&B, &MR, 2) == 1, "phase-start check: threatened");
        ai_resolve(2, &m);
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        ai_resolve(2, &m);
        CHECK(m.flags & AIF_ALERT, "threatened -> alerted");
        CHECK(c.action == ACT_ATTACK || c.x != 11 || c.y != 0, "alerted HOLD acts like AGGRESSIVE");
        battle_move(1, 2, 9);
        ai_resolve(2, &m);
        CHECK(m.flags & AIF_ALERT, "alert persists");
    }
    /* designated target: approach it rather than the nearest opponent */
    reset();
    {
        AiMode m; AiCommand c;
        battle_move(0, 0, 9); battle_move(1, 11, 9);
        host_units[2].x = 6; host_units[2].y = 9; host_occ[5 * 12 + 4] = 0; host_occ[9 * 12 + 6] = 2;
        m.mode = AI_AGGRESSIVE; m.target = 0; m.leash = 3; m.flags = 0;
        ai_invalidate(&S);
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK(c.x < 6, "hunts unit 0 to the west (%u,%u)", c.x, c.y);
        m.target = 1; host_units[2].action_flags = 0;
        ai_decide_mode(&B, &S, &MR, 2, &m, &c);
        CHECK(c.x > 6, "hunts unit 1 to the east (%u,%u)", c.x, c.y);
    }
    /* AI support strategy: an enemy strategist heals a wounded ally; fire pot finishes a weak opponent */
    reset();
    {
        AiCommand c;
        StrategyResult sr;
        battle_spawn(&B, 3, 0xFFFF, 9, FAC_ENEMY, 5, 10, 10);   /* enemy 文士 */
        battle_spawn(&B, 4, 0xFFFF, 0, FAC_ENEMY, 5, 10, 9);
        host_units[2].x = 11; host_units[2].y = 0; host_occ[5 * 12 + 4] = 0; host_occ[0 * 12 + 11] = 2;
        host_units[4].hp = 20;
        ai_invalidate(&S);
        ai_decide(&B, &S, &MR, 3, AI_AGGRESSIVE, &c);
        CHECK(c.action == ACT_STRATEGY && strategy_defs[c.param].effect == FX_HEAL && c.tx == 10 && c.ty == 9,
              "AI heals its wounded ally (action %u param %u at %u,%u)", c.action, c.param, c.tx, c.ty);
        if (c.action == ACT_STRATEGY) {
            battle_move(3, c.x, c.y);
            CHECK(strategy_cast(&B, 3, c.param, c.tx, c.ty, &sr) == SR_OK && host_units[4].hp > 20, "heal command legal");
        }
    }
    reset();
    {
        AiCommand c;
        host_units[2].x = 6; host_units[2].y = 5; host_occ[5 * 12 + 4] = 0; host_occ[5 * 12 + 6] = 2;
        host_units[2].mp = 0;
        host_units[1].hp = 100;                                  /* in pot range from (5,5)/(6,5)... */
        inv_set(2, 6, 8);                                        /* 火油罐 */
        ai_invalidate(&S);
        ai_decide(&B, &S, &MR, 2, AI_AGGRESSIVE, &c);
        CHECK(c.action == ACT_ITEM && c.param == 6, "AI throws the fire pot for the kill (action %u)", c.action);
        if (c.action == ACT_ITEM) {
            battle_move(2, c.x, c.y);
            CHECK(item_use(&B, 2, c.param, c.tx, c.ty, &r) == IR_OK && r.killed, "pot command legal and lethal");
        }
    }
    /* AI records survive a battle save (tag 10) */
    reset();
    {
        SaveUi ui;
        AiMode m;
        memset(&ui, 0, sizeof ui);
        ai_groups_init(0, 0);
        ai_set(4, AI_ESCORT, 2, 2);
        save_cache_reset();
        CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "save with AI records");
        ai_groups_init(0, 0);
        CHECK(load_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "load");
        ai_resolve(4, &m);
        CHECK(m.mode == AI_ESCORT && m.target == 2 && m.leash == 2, "override restored");
    }

    /* treasures and the prize queue (7.4) */
    reset();
    {
        uint8_t s;
        host_n_treasures = 2;
        host_treasures[0].x = 7; host_treasures[0].y = 7; host_treasures[0].item = 0x60; host_treasures[0].flag = 0x100;
        host_treasures[1].x = 8; host_treasures[1].y = 7; host_treasures[1].item = 0x50; host_treasures[1].flag = 0x101;
        CHECK(treasure_at(7, 7) == 0 && treasure_at(8, 7) == 1 && treasure_at(6, 7) == 0xFF, "treasure lookup");
        for (s = 0; s < INV_SLOTS; s++) inv_set(0, s, 0);
        CHECK(treasure_take(0, 0) == IR_FULL && treasure_at(7, 7) == 0 && !vm_flag_get(SCOPE_BATTLE, 0x100),
              "full receiver: nothing taken, nothing marked");
        CHECK(inv_first_free(&B, FAC_PLAYER) == 1, "first free officer");
        CHECK(treasure_take(0, 1) == IR_OK && inv_get(1, 0) == 0x60 && treasure_at(7, 7) == 0xFF, "claimed by another officer");
        CHECK(treasure_take(0, 1) == IR_NO_TARGET && inv_count(1) == 1, "only once");
        host_n_treasures = 0;
        prize_clear();
        for (s = 0; s < PRIZE_MAX; s++) CHECK(prize_push((uint8_t)(s & 3)), "push %u", s);
        CHECK(!prize_push(1) && prize_count() == PRIZE_MAX, "17th prize refused");
        prize_remove(0);
        CHECK(prize_count() == 15 && prize_get(0) == 1 && prize_get(14) == 3, "remove keeps order");
        CHECK(!prize_push(0x0F), "unknown item id refused");
        {
            SaveUi ui;
            memset(&ui, 0, sizeof ui);
            save_cache_reset();
            CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "save with prizes");
            prize_clear();
            CHECK(load_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK && prize_count() == 15 && prize_get(0) == 1, "prize queue restored");
        }
    }

    /* danger view (8.4): exact reach + range */
    reset();
    {
        static uint8_t bits[512];
        uint8_t x, y, n = 0, ok = 1;
        memset(bits, 0, sizeof bits);
        ai_danger_add(&B, &MR, 2, bits);                    /* enemy infantry at (4,5), move 4, range 1 */
        for (y = 0; y < 12; y++) for (x = 0; x < 12; x++) {
            uint16_t c = (uint16_t)y * 12 + x;
            uint8_t on = (bits[c >> 3] >> (c & 7)) & 1, dd = (uint8_t)((x > 4 ? x - 4 : 4 - x) + (y > 5 ? y - 5 : 5 - y));
            n += on;
            if (dd > 5 && on) ok = 0;                            /* never beyond move + range */
            if (x == 9 && y == 5 && !on) ok = 0;                  /* open plain east: full reach */
        }
        CHECK(ok && n >= 45, "danger = move 4 + range 1 on open ground (%u cells)", n);
        host_terrain[5 * 12 + 6] = 8; host_terrain[4 * 12 + 6] = 8; host_terrain[6 * 12 + 6] = 8;   /* wall */
        memset(bits, 0, sizeof bits);
        ai_danger_add(&B, &MR, 2, bits);
        CHECK(!((bits[(5 * 12 + 9) >> 3] >> ((5 * 12 + 9) & 7)) & 1), "walls shorten the danger zone");
    }

    /* roster (5.6, B.2), continuity (2.4), camp saves (11.6) */
    reset();
    {
        RosterRec rr;
        SaveUi ui;
        uint8_t a, k;
        uint16_t mhp;
        roster_clear();
        a = roster_add(2, 6, 3, 80, 50, 90);                 /* 关羽: cavalry L3, CMD 90 */
        CHECK(a == 0 && roster_add(2, 6, 1, 1, 1, 1) == 0xFF && roster_count() == 1, "add once per character");
        CHECK(roster_add(8, 9, 1, 30, 85, 60) == 1 && roster_find(8) == 1 && roster_find(5) == 0xFF, "find");
        CHECK(named_get(2) == FATE_OWNED && named_get(3) == FATE_NONE, "named states");
        roster_read(0, &rr); rr.items[0] = 0x41; rr.items[1] = 0; rr.equip[0] = 0; rr.xp = 40; roster_write(0, &rr);
        roster_deploy(&B, 0, 6, 9, 9, 1);
        mhp = (uint16_t)(class_defs[6].hp + 12u * 2u + 2u * 90u);
        CHECK(host_units[6].max_hp == mhp && host_units[6].hp == mhp, "derived from the record's CMD (%u vs %u)", host_units[6].max_hp, mhp);
        CHECK(inv_get(6, 0) == 0x41 && inv_equipped(6, EQ_WEAPON) == 0 && host_units[6].exp == 40, "items, equipment, XP come along");
        host_units[6].hp = 50; host_units[6].morale = 70; host_units[6].level = 4;
        inv_set(6, 3, 1);
        roster_sync(0, 6);
        roster_read(0, &rr);
        CHECK(rr.hp == 50 && rr.morale == 70 && rr.level == 4 && rr.items[6] == 1, "sync back (hp/morale/L/items)");
        roster_deploy(&B, 0, 6, 9, 9, 0);
        CHECK(host_units[6].hp == 50 && host_units[6].morale == 70, "NO_REPLENISH carries hp/morale");
        roster_deploy(&B, 0, 6, 9, 9, 1);
        CHECK(host_units[6].hp == host_units[6].max_hp && host_units[6].morale == 100, "NORMAL replenishes");
        /* camp save: roster + named + campaign area + gold + next battle */
        memset(&ui, 0, sizeof ui);
        ui.battle_id = 2; ui.gold = 1234;
        host_campaign[5] = 0x5A;
        save_cache_reset();
        CHECK(save_camp(SAVE_SLOT_NORMAL0, &ui) == SAVE_OK && save_kind(SAVE_SLOT_NORMAL0) == SAVE_KIND_CAMP, "camp save");
        roster_clear(); host_campaign[5] = 0; memset(&ui, 0, sizeof ui);
        save_cache_reset();
        CHECK(load_battle(SAVE_SLOT_NORMAL0, &B, &ui) != SAVE_OK, "a camp save is not a battle save");
        CHECK(load_camp(SAVE_SLOT_NORMAL0, &ui) == SAVE_OK && ui.battle_id == 2 && ui.gold == 1234, "camp load");
        CHECK(roster_count() == 2 && roster_find(8) == 1 && named_get(8) == FATE_OWNED && host_campaign[5] == 0x5A,
              "roster / named states / campaign area restored");
        roster_read(0, &rr);
        CHECK(rr.items[6] == 1 && rr.level == 4, "record contents restored");
        /* battle save with a non-empty roster: the loader walks the TLVs */
        memset(&ui, 0, sizeof ui);
        for (k = 0; k < 3; k++) host_units[k].action_flags = 0;
        CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "battle save with roster");
        roster_clear();
        CHECK(load_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK && roster_count() == 2 && host_units[6].max_hp == mhp + 12,
              "battle load with roster (units after a 2-record roster)");
    }

    /* undo log (11.6): capacity is reserved before each write; a full log blocks the write */
    reset();
    {
        uint16_t hp0 = host_units[1].hp;
        undo_begin();
        { ActiveUnit u = host_units[1]; u.hp = 7; unit_store(1, &u); }   /* via the hook: logged */
        inv_set(1, 2, 8);
        CHECK(undo_used() > 40u, "unit + item writes logged (%u B)", undo_used());
        undo_rollback();
        CHECK(host_units[1].hp == hp0 && inv_get(1, 2) == ITEM_NONE, "rollback restores unit and item block (hp %u vs %u, item %u)", host_units[1].hp, hp0, inv_get(1, 2));
        undo_begin();
        {
            ActiveUnit u = host_units[2];
            uint16_t k, ok = 1;
            for (k = 0; k < 200u && ok; k++) { u.hp = (uint16_t)k; unit_store(2, &u); ok = !undo_overflowed(); }
            CHECK(undo_overflowed() && undo_used() <= UNDO_CAP, "log fills up and stops (%u B)", undo_used());
            CHECK(host_units[2].hp != u.hp, "the write that did not fit was not performed");
        }
        undo_rollback();
        CHECK(host_units[2].hp == host_units[2].max_hp, "rollback after overflow restores the original");
    }
    /* pre-battle snapshot (11.6): roster + gold + deployment, 8 KiB copies in banks 13/14 */
    reset();
    {
        SaveUi ui;
        uint8_t dep[9] = {2, 1, 0, 0, 0, 3, 0, 0, 0}, out[65];
        Battle sb;
        roster_clear();
        roster_add(1, 0, 1, 70, 60, 70);
        memset(&ui, 0, sizeof ui); ui.battle_id = 1; ui.gold = 77;
        B.round = 9;
        save_cache_reset();
        CHECK(save_snapshot(&B, &ui, dep) == SAVE_OK, "snapshot written");
        roster_clear();
        memset(&ui, 0, sizeof ui);
        CHECK(load_snapshot(&sb, &ui, out) == SAVE_OK && ui.battle_id == 1 && ui.gold == 77 && sb.round == 9,
              "snapshot: battle id, gold, starting round");
        CHECK(out[0] == 2 && out[1] == 1 && out[5] == 3 && roster_count() == 1, "snapshot: deployment and roster");
        CHECK(host_sram[13][0] == 'Y' || host_sram[14][0] == 'Y', "lives in SRAM bank 13/14");
    }

    /* 6.6 objectives: defeat a named general, hold out, protect an NPC */
    reset();
    battle_spawn(&B, 3, 0xFFFF, 0, FAC_ENEMY, 1, 9, 9);
    B.vic_kind = VIC_DEFEAT_UNIT; B.vic_unit = 3; B.protect = NO_UNIT;
    CHECK(battle_check_outcome(&B, 0) == OUT_NONE, "target general still standing");
    host_units[3].hp = 0; host_units[3].action_flags |= AF_RETREAT;
    CHECK(battle_check_outcome(&B, 0) == OUT_WIN, "target general removed -> win with enemies left");
    reset();
    B.vic_kind = VIC_SURVIVE; B.vic_round = 5; B.vic_unit = NO_UNIT; B.protect = 1;
    B.round = 4;
    CHECK(battle_check_outcome(&B, 1) == OUT_NONE, "round 4 of 5: keep holding");
    B.round = 5;
    CHECK(battle_check_outcome(&B, 0) == OUT_NONE && battle_check_outcome(&B, 1) == OUT_WIN, "survive: win at the end of round 5");
    reset();
    B.vic_kind = VIC_ALL_ENEMIES; B.protect = 1;
    host_units[1].hp = 0; host_units[1].action_flags |= AF_RETREAT;
    CHECK(battle_check_outcome(&B, 0) == OUT_LOSE, "protected ally lost -> defeat");

    /* VIC_REACH: an escorted unit reaches the safe area */
    reset();
    B.vic_kind = VIC_REACH; B.vic_unit = 1; B.protect = NO_UNIT;
    B.vic_x0 = 8; B.vic_y0 = 0; B.vic_x1 = 11; B.vic_y1 = 2;
    CHECK(battle_check_outcome(&B, 0) == OUT_NONE, "escort not there yet");
    battle_move(1, 9, 1);
    CHECK(battle_check_outcome(&B, 0) == OUT_WIN, "escort inside the area -> win");
    reset();
    B.vic_kind = VIC_REACH; B.vic_unit = 1; B.protect = NO_UNIT;
    B.vic_x0 = 8; B.vic_y0 = 0; B.vic_x1 = 11; B.vic_y1 = 2;
    host_units[1].hp = 0; host_units[1].action_flags |= AF_RETREAT;
    CHECK(battle_check_outcome(&B, 0) == OUT_LOSE, "the unit that must arrive is gone -> defeat (stated on the conditions page)");

    /* battle save carries the inventory block (tag 9) */
    reset();
    {
        SaveUi ui;
        memset(&ui, 0, sizeof ui);
        inv_set(0, 1, 2); inv_set(2, 7, 8);
        save_cache_reset();
        CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "save");
        inv_clear_all();
        CHECK(load_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "load");
        CHECK(inv_get(0, 1) == 2 && inv_get(2, 7) == 8 && inv_get(1, 0) == ITEM_NONE, "inventory restored");
    }

    printf("items/ai: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
