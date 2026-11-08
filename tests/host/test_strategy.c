/* Strategy tests (design doc 5.3, 6.3, 6.4). */
#include <stdio.h>
#include <string.h>
#include "../../src/core/strategy.h"
#include "../../src/core/grid.h"
#include "../../src/core/rules.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Battle B;

static void reset(void)
{
    uint8_t i;
    grid_w = 12; grid_h = 12;
    memset(host_terrain, 0, sizeof host_terrain); memset(host_occ, 0, sizeof host_occ);
    memset(host_units, 0, sizeof host_units);
    for (i = 0; i < MAX_UNITS; i++) { host_base[i].str = 40; host_base[i].intl = 90; host_base[i].cmd = 60; }
    memset(&B, 0, sizeof B); rng_seed(&B.rng, 7); B.round = 1; B.turn_limit = 30;
    battle_spawn(&B, 0, 1, 9, FAC_PLAYER, 1, 2, 5);          /* 文士 caster */
    host_base[1].intl = 60;
    battle_spawn(&B, 1, 0xFFFF, 0, FAC_ENEMY, 1, 4, 5);      /* infantry target */
}

int main(void)
{
    StrategyResult r;
    Rng ref;
    uint32_t d0;
    uint16_t hp0, mp0;

    /* fire on forest: hit/damage from the same 2 draws, BURN attached, MP spent */
    reset();
    host_terrain[5 * 12 + 4] = 2;                                  /* target in forest */
    CHECK(strategy_known(&host_units[0], 0), "文士 knows 火攻 at L1");
    CHECK(!strategy_known(&host_units[0], 1), "烈焰 needs L12");
    ref = B.rng; d0 = B.rng.draws; hp0 = host_units[1].hp; mp0 = host_units[0].mp;
    CHECK(strategy_cast(&B, 0, 0, 4, 5, &r) == SR_OK && r.n == 1, "cast fire");
    {
        uint16_t roll = rng_rand(&ref, 100);
        uint8_t var = (uint8_t)(90 + rng_rand(&ref, 21));
        uint8_t pct = rules_mag_hit(90, 60, 100);
        uint16_t dmg = rules_mag_damage(20, host_units[0].mag, host_units[1].res, 100, 125, var, 0);
        CHECK(r.hit[0] == (roll < pct), "hit roll");
        if (r.hit[0]) {
            CHECK(r.amount[0] == dmg && host_units[1].hp == hp0 - dmg, "damage %u vs %u", r.amount[0], dmg);
            CHECK(host_units[1].status_mask & (1u << ST_BURN) && host_units[1].ttl[ST_BURN] == 2, "BURN 2 on fire hit");
            CHECK(!(host_units[1].expiry_eligible_mask & (1u << ST_BURN)), "fresh status not eligible this phase");
        }
    }
    CHECK(B.rng.draws - d0 == 2, "2 draws per hostile target");
    CHECK(host_units[0].mp == mp0 - 4 && (host_units[0].action_flags & AF_ACTED), "MP spent, caster acted");
    CHECK(!(host_units[1].action_flags & AF_COUNTER), "strategies never trigger counters");

    /* weather / terrain conditions and validation codes */
    reset();
    B.weather = 1;
    CHECK(strategy_check(&B, 0, 0, 4, 5) == SR_WEATHER, "fire not in rain");
    B.weather = 0;
    CHECK(strategy_check(&B, 0, 3, 4, 5) == SR_NO_TARGET, "water needs a water cell");
    host_terrain[5 * 12 + 4] = 6;
    CHECK(strategy_check(&B, 0, 3, 4, 5) == SR_OK, "water on shallow ok");
    CHECK(strategy_check(&B, 0, 0, 5, 5) == SR_RANGE || strategy_check(&B, 0, 0, 5, 5) == SR_NO_TARGET, "range 2");
    CHECK(strategy_check(&B, 0, 0, 2, 5) == SR_NO_TARGET, "own cell has no hostile target");
    host_terrain[5 * 12 + 3] = 8;                                  /* wall between caster and target */
    CHECK(strategy_check(&B, 0, 3, 4, 5) == SR_LOS, "wall blocks the line of sight");
    host_terrain[5 * 12 + 3] = 0;
    host_units[0].mp = 3;
    CHECK(strategy_check(&B, 0, 0, 4, 5) == SR_NO_MP, "not enough MP");
    host_units[0].mp = 20; host_units[0].status_mask = 1u << ST_SILENCE; host_units[0].ttl[ST_SILENCE] = 2;
    CHECK(strategy_check(&B, 0, 0, 4, 5) == SR_SILENCED, "silence blocks strategies");
    host_units[1].action_flags = 0;
    CHECK(strategy_check(&B, 1, 0, 2, 5) == SR_NOT_LEARNED, "infantry does not know fire");

    /* heal: min(power + MAG/2, deficit), no RNG; ally only (may target self) */
    reset();
    battle_spawn(&B, 2, 2, 0, FAC_PLAYER, 1, 2, 6);
    host_units[2].hp = 100;
    d0 = B.rng.draws;
    CHECK(strategy_cast(&B, 0, 9, 2, 6, &r) == SR_OK && r.n == 1, "heal ally");
    CHECK(r.amount[0] == 80 + host_units[0].mag / 2 && host_units[2].hp == 100 + r.amount[0], "heal amount %u", r.amount[0]);
    CHECK(B.rng.draws == d0, "heal draws no RNG");
    battle_spawn(&B, 3, 0xFFFF, 0, FAC_ENEMY, 1, 3, 5);         /* adjacent enemy */
    CHECK(strategy_check(&B, 0, 9, 3, 5) == SR_NO_TARGET, "heal cannot target an enemy");
    reset();
    host_units[0].hp = host_units[0].max_hp - 30;
    CHECK(strategy_cast(&B, 0, 9, 2, 5, &r) == SR_OK && r.amount[0] == 30, "self heal capped at the deficit");
    CHECK(host_units[0].mp == host_units[0].max_mp - 4, "self heal still spends MP once");

    /* radius 1 (焚营 at L24 via 方士): centre + 4 neighbours, hostile only, instance order */
    reset();
    battle_spawn(&B, 3, 0xFFFF, 0, FAC_ENEMY, 1, 4, 4);
    battle_spawn(&B, 4, 0xFFFF, 0, FAC_ENEMY, 1, 5, 5);
    battle_spawn(&B, 5, 2, 0, FAC_PLAYER, 1, 4, 6);             /* ally inside the area: untouched */
    host_units[0].class_id = 16; host_units[0].level = 24; host_units[0].mp = 99;
    d0 = B.rng.draws;
    CHECK(strategy_cast(&B, 0, 2, 4, 5, &r) == SR_OK, "焚营 cast");
    CHECK(r.n == 3 && r.unit[0] == 1 && r.unit[1] == 3 && r.unit[2] == 4, "3 hostile targets in instance order (n=%u)", r.n);
    CHECK(B.rng.draws - d0 == 6, "2 draws per hostile target");
    CHECK(host_units[5].hp == host_units[5].max_hp, "allies are never hurt");

    /* status: ROOT ttl 2, reapply keeps max, cleanse removes all */
    reset();
    host_units[0].level = 1;
    {
        uint8_t tries = 0;
        do { host_units[0].action_flags = 0; host_units[0].mp = 20; strategy_cast(&B, 0, 15, 4, 5, &r); } while (!r.hit[0] && ++tries < 20);
        CHECK(host_units[1].status_mask & (1u << ST_ROOT) && host_units[1].ttl[ST_ROOT] == 2, "ROOT applied ttl 2");
        host_units[1].ttl[ST_ROOT] = 1;
        tries = 0;
        do { host_units[0].action_flags = 0; host_units[0].mp = 20; strategy_cast(&B, 0, 15, 4, 5, &r); } while (!r.hit[0] && ++tries < 20);
        CHECK(host_units[1].ttl[ST_ROOT] == 2, "reapply takes max(old,new)");
    }
    reset();
    battle_spawn(&B, 2, 2, 0, FAC_PLAYER, 1, 2, 6);
    host_units[2].status_mask = (1u << ST_POISON) | (1u << ST_ROOT); host_units[2].ttl[ST_POISON] = 2;
    CHECK(strategy_cast(&B, 0, 19, 2, 6, &r) == SR_OK && host_units[2].status_mask == 0, "cleanse removes every status");
    /* killing blow retreats the target and can end the battle */
    reset();
    host_units[1].hp = 1;
    {
        uint8_t tries = 0;
        do { host_units[0].action_flags = 0; host_units[0].mp = 20; strategy_cast(&B, 0, 6, 4, 5, &r); } while (r.n && !r.hit[0] && ++tries < 30);
    }
    host_terrain[5 * 12 + 4] = 5;                                   /* (rock needs hill; retry on hill) */
    {
        uint8_t tries = 0;
        do { host_units[0].action_flags = 0; host_units[0].mp = 20; strategy_cast(&B, 0, 6, 4, 5, &r); } while ((!r.n || !r.hit[0]) && ++tries < 30);
    }
    CHECK(host_units[1].hp == 0 && (host_units[1].action_flags & AF_RETREAT) && B.outcome == OUT_WIN, "kill -> retreat -> win");
    printf("strategy: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
