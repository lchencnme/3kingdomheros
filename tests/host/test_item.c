/* Items, inventory, exchange (design doc 7.1-7.3) and AI strategy/item candidates (8.3). */
#include <stdio.h>
#include <string.h>
#include "../../src/core/item.h"
#include "../../src/core/ai.h"
#include "../../src/core/strategy.h"
#include "../../src/core/save.h"
#include "../../src/core/grid.h"
#include "../../src/core/rules.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];
extern uint8_t host_sram[16][8192];

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
