/* Host tests for battle resolution, phases, outcome and AI (design doc 4, 5, 6, 8). */
#include <stdio.h>
#include <string.h>
#include "../../src/core/battle.h"
#include "../../src/core/ai.h"
#include "../../src/core/path.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Battle B;
static MoveRange MR;
static AiScratch AS;

static void reset(uint8_t w, uint8_t h)
{
    uint8_t i;
    grid_w = w; grid_h = h;
    memset(host_terrain, 0, sizeof host_terrain);
    memset(host_occ, 0, sizeof host_occ);
    memset(host_units, 0, sizeof host_units);
    for (i = 0; i < MAX_UNITS; i++) { host_base[i].str = 70; host_base[i].intl = 60; host_base[i].cmd = 70; }
    memset(&B, 0, sizeof B);
    rng_seed(&B.rng, 1);
    B.round = 1; B.turn_limit = 30; B.leader = 0;
    ai_invalidate(&AS);
}

static void occupancy_consistent(const char *where)
{
    uint8_t i;
    uint16_t cells = (uint16_t)grid_w * grid_h, c, n = 0;
    for (c = 0; c < cells; c++) if (host_occ[c]) n++;
    for (i = 0; i < B.n_units; i++) {
        const ActiveUnit *u = &host_units[i];
        if (!unit_alive(u)) continue;
        n--;
        CHECK(host_occ[(uint16_t)u->y * grid_w + u->x] == faction_side(u->faction), "%s: unit %u occ", where, i);
        CHECK(u->hp <= u->max_hp, "%s: hp<=max unit %u", where, i);
    }
    CHECK(n == 0, "%s: stray occupancy %u", where, n);
}

static void test_attack_basic(void)
{
    AttackResult r;
    uint32_t d0;
    Rng ref;
    uint16_t roll, var, exp_dmg;
    PhysIn in;
    reset(8, 8);
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 2, 2);     /* 刀盾兵 */
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 3, 2);
    CHECK(host_units[0].max_hp == 300 && host_units[0].hp == 300, "spawn maxhp");
    ref = B.rng;
    d0 = B.rng.draws;
    battle_attack(&B, 0, 1, &r);
    roll = rng_rand(&ref, 100); var = (uint16_t)(90 + rng_rand(&ref, 21));
    in.atk = host_units[0].atk; in.def = host_units[1].def; in.hp = 300; in.max_hp = 300;
    in.fam_a = in.fam_d = FAM_INFANTRY; in.morale = 100; in.terrain_d = 0; in.rain = 0; in.variance = (uint8_t)var;
    exp_dmg = roll < rules_phys_hit(host_units[0].agility, host_units[1].agility, 100, 0) ? rules_phys_damage(&in, 0) : 0;
    CHECK(r.hit == (exp_dmg > 0) && r.dmg == exp_dmg, "attack dmg %u vs %u", r.dmg, exp_dmg);
    CHECK(r.countered == 1, "infantry counters");
    CHECK(B.rng.draws - d0 == 4, "2 draws + 2 counter draws, got %u", (unsigned)(B.rng.draws - d0));
    CHECK(host_units[1].action_flags & AF_COUNTER, "counter flag spent");
    CHECK(host_units[0].action_flags & AF_ACTED, "attacker acted");
    CHECK(r.xp == (r.hit ? 10 : 1), "xp %u", r.xp);
    /* second attack in the same phase: no second counter */
    host_units[0].action_flags = 0;
    d0 = B.rng.draws;
    battle_attack(&B, 0, 1, &r);
    CHECK(r.countered == 0 && B.rng.draws - d0 == 2, "one counter per phase");
    occupancy_consistent("attack_basic");
}

static void test_counter_rules(void)
{
    AttackResult r;
    reset(8, 8);
    battle_spawn(&B, 0, 1, 3, FAC_PLAYER, 1, 1, 1);     /* 弓手 */
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 3, 1);   /* infantry, distance 2 */
    CHECK(battle_in_range(&host_units[0], 1, 1, &host_units[1]), "bow range 2");
    battle_attack(&B, 0, 1, &r);
    CHECK(!r.countered, "melee cannot counter at range 2");
    host_units[0].action_flags = 0;
    battle_move(1, 2, 1);                                         /* adjacent: bow cannot shoot */
    CHECK(!battle_in_range(&host_units[0], 1, 1, &host_units[1]), "bow cannot hit adjacent");
    host_units[1].action_flags = 0;
    battle_attack(&B, 1, 0, &r);
    CHECK(!r.countered, "bow has counter=false");
    /* wall blocks LOS */
    reset(5, 3);
    host_terrain[1 * 5 + 2] = 8;
    battle_spawn(&B, 0, 1, 3, FAC_PLAYER, 1, 1, 1);
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 3, 1);
    CHECK(!battle_in_range(&host_units[0], 1, 1, &host_units[1]), "wall blocks arrow");
}

static void test_kill_and_outcome(void)
{
    AttackResult r;
    uint8_t tries = 0;
    reset(8, 8);
    battle_spawn(&B, 0, 1, 6, FAC_PLAYER, 30, 2, 2);    /* strong cavalry */
    battle_spawn(&B, 1, ANON_CHARACTER, 3, FAC_ENEMY, 1, 3, 2);   /* weak archer */
    host_units[1].hp = 5;
    do { host_units[0].action_flags = 0; battle_attack(&B, 0, 1, &r); } while (!r.killed && ++tries < 20);
    CHECK(r.killed && host_units[1].hp == 0 && (host_units[1].action_flags & AF_RETREAT), "killed");
    CHECK(host_occ[2 * 8 + 3] == 0, "occupancy cleared on retreat");
    CHECK(!r.countered, "no counter after kill");
    CHECK(B.outcome == OUT_WIN, "all enemies removed -> win");
    /* leader loss has priority over simultaneous win */
    reset(8, 8);
    battle_spawn(&B, 0, 1, 3, FAC_PLAYER, 1, 2, 2);
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 3, 2);
    host_units[0].hp = 0; host_units[0].action_flags = AF_RETREAT;
    host_units[1].hp = 0; host_units[1].action_flags = AF_RETREAT;
    CHECK(battle_check_outcome(&B, 0) == OUT_LOSE, "hard loss first");
    /* round limit: lose only at the round-end checkpoint of round N */
    reset(8, 8);
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 0, 0);
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 7, 7);
    B.turn_limit = 2;
    battle_phase_start(&B, FAC_PLAYER); battle_phase_end(&B);
    battle_phase_start(&B, FAC_ENEMY); battle_phase_end(&B);
    CHECK(B.outcome == OUT_NONE && B.round == 2, "round 1 passes");
    battle_phase_start(&B, FAC_PLAYER); battle_phase_end(&B);
    CHECK(B.outcome == OUT_NONE, "round N player phase still playable");
    battle_phase_start(&B, FAC_ENEMY); battle_phase_end(&B);
    CHECK(B.outcome == OUT_LOSE, "after round N enemy phase -> lose");
}

static void test_phases(void)
{
    reset(6, 6);
    host_terrain[0] = 3;                                          /* village at (0,0) */
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 0, 0);
    battle_spawn(&B, 1, 2, 0, FAC_PLAYER, 1, 2, 0);
    battle_spawn(&B, 2, ANON_CHARACTER, 0, FAC_ENEMY, 1, 5, 5);
    host_units[0].hp = 100;                                       /* village heal ceil(300*10%)=30 */
    host_units[1].status_mask = 1u << ST_POISON; host_units[1].ttl[ST_POISON] = 1;
    battle_phase_start(&B, FAC_PLAYER);
    CHECK(host_units[0].hp == 130, "village heal %u", host_units[0].hp);
    CHECK(host_units[1].hp == 270, "poison ceil(300/10)=30 -> %u", host_units[1].hp);
    battle_phase_end(&B);
    CHECK(!(host_units[1].status_mask & (1u << ST_POISON)), "TTL1 expires at own phase end");
    /* a status applied during the phase (not eligible) survives the phase end */
    battle_phase_start(&B, FAC_PLAYER);
    host_units[1].status_mask = 1u << ST_ROOT; host_units[1].ttl[ST_ROOT] = 1;
    battle_phase_end(&B);
    CHECK(host_units[1].status_mask & (1u << ST_ROOT), "fresh status not ticked");
    battle_phase_start(&B, FAC_PLAYER); battle_phase_end(&B);
    CHECK(!(host_units[1].status_mask & (1u << ST_ROOT)), "ticked next own phase");
}

static void test_level_up(void)
{
    AttackResult r;
    reset(8, 8);
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 2, 2);
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 20, 3, 2);
    host_units[0].exp = 95;
    host_units[0].hp = 250;                                        /* deficit 50 */
    do { host_units[0].action_flags = 0; host_units[1].action_flags = 0; battle_attack(&B, 0, 1, &r); } while (!r.hit);
    CHECK(r.levels == 1 && host_units[0].level == 2, "level up");
    CHECK(host_units[0].max_hp == 312, "L2 maxhp 312");
    /* deficit kept (minus any counter damage taken this exchange) */
    CHECK(host_units[0].hp == 312 - 50 - r.c_dmg, "deficit kept: %u", host_units[0].hp);
}

static void test_ai(void)
{
    AiCommand c;
    uint16_t f0;
    reset(12, 8);
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 1, 4);
    battle_spawn(&B, 1, ANON_CHARACTER, 0, FAC_ENEMY, 1, 10, 4);
    battle_phase_start(&B, FAC_ENEMY);
    ai_decide(&B, &AS, &MR, 1, AI_AGGRESSIVE, &c);
    CHECK(c.action == ACT_WAIT, "out of reach -> wait");
    f0 = 9;
    CHECK(c.x + c.y > 0 && (uint8_t)(c.x > 1 ? c.x - 1 : 1 - c.x) + (c.y > 4 ? c.y - 4 : 4 - c.y) < f0, "approaches leader: (%u,%u)", c.x, c.y);
    CHECK(c.x == 6 && c.y == 4, "aggressive: full approach along row -> (%u,%u)", c.x, c.y);
    /* in reach: attack */
    battle_move(1, 4, 4);
    host_units[1].action_flags = 0;
    ai_decide(&B, &AS, &MR, 1, AI_AGGRESSIVE, &c);
    CHECK(c.action == ACT_ATTACK && c.target == 0, "attacks when reachable");
    CHECK((c.x == 2 && c.y == 4) || (c.x == 1 && (c.y == 3 || c.y == 5)), "adjacent endpoint (%u,%u)", c.x, c.y);
    /* kill priority: a weak unit in reach is chosen over a full-hp one */
    battle_spawn(&B, 2, 2, 0, FAC_PLAYER, 1, 6, 6);
    host_units[2].hp = 20;
    ai_invalidate(&AS);
    ai_decide(&B, &AS, &MR, 1, AI_AGGRESSIVE, &c);
    CHECK(c.action == ACT_ATTACK && c.target == 2, "kill priority -> target %u", c.target);
    /* HOLD never leaves its cell */
    ai_decide(&B, &AS, &MR, 1, AI_HOLD, &c);
    CHECK(c.x == 4 && c.y == 4, "HOLD stays");
    /* determinism: same input, same command */
    {
        AiCommand c2;
        ai_decide(&B, &AS, &MR, 1, AI_AGGRESSIVE, &c);
        ai_invalidate(&AS);
        ai_decide(&B, &AS, &MR, 1, AI_AGGRESSIVE, &c2);
        CHECK(memcmp(&c, &c2, sizeof c) == 0, "deterministic AI");
    }
}

/* full self-play of a TECH01-like battle: both sides AI; replay must be identical */
static uint32_t self_play(uint8_t *rounds_out, uint8_t *outcome_out)
{
    static const uint8_t setup[7][4] = {{0, 0, 4, 12}, {6, 0, 5, 11}, {6, 0, 5, 13},
                                        {0, 2, 20, 12}, {3, 2, 23, 11}, {0, 2, 24, 13}, {3, 2, 26, 12}};
    uint8_t i, ph;
    uint32_t h = 2166136261u;
    reset(32, 24);
    for (i = 0; i < 32; i++) { host_terrain[13 * 1 + i * 0] = 0; }
    for (i = 0; i < 24; i++) { host_terrain[i * 32 + 14] = 7; host_terrain[i * 32 + 15] = 7; }
    host_terrain[12 * 32 + 14] = 9; host_terrain[12 * 32 + 15] = 9;    /* bridge */
    for (i = 0; i < 7; i++) battle_spawn(&B, i, i < 3 ? (uint16_t)(i + 1) : ANON_CHARACTER, setup[i][0], setup[i][1], 1, setup[i][2], setup[i][3]);
    while (B.outcome == OUT_NONE && B.round <= 40) {
        for (ph = 0; ph < 3 && B.outcome == OUT_NONE; ph++) {
            battle_phase_start(&B, ph);
            ai_invalidate(&AS);
            for (i = 0; i < B.n_units && B.outcome == OUT_NONE; i++) {
                AiCommand c;
                AttackResult r;
                if (host_units[i].faction != ph || !battle_can_act(&host_units[i])) continue;
                ai_decide(&B, &AS, &MR, i, AI_AGGRESSIVE, &c);
                if (c.x != host_units[i].x || c.y != host_units[i].y) {
                    CHECK(path_endpoint_ok(&MR, c.x, c.y), "AI endpoint legal");
                    battle_move(i, c.x, c.y);
                }
                if (c.action == ACT_ATTACK) {
                    CHECK(battle_in_range(&host_units[i], c.x, c.y, &host_units[c.target]), "AI target in range");
                    battle_attack(&B, i, c.target, &r);
                } else host_units[i].action_flags |= AF_ACTED;
                occupancy_consistent("self_play");
            }
            if (B.outcome == OUT_NONE) battle_phase_end(&B);
        }
    }
    for (i = 0; i < B.n_units; i++) {
        const uint8_t *p = (const uint8_t *)&host_units[i];
        uint8_t k;
        for (k = 0; k < sizeof(ActiveUnit); k++) h = (h ^ p[k]) * 16777619u;
    }
    h ^= B.rng.state;
    *rounds_out = (uint8_t)B.round; *outcome_out = B.outcome;
    return h;
}

int main(void)
{
    uint8_t r1, o1, r2, o2;
    uint32_t h1, h2;
    test_attack_basic();
    test_counter_rules();
    test_kill_and_outcome();
    test_phases();
    test_level_up();
    test_ai();
    h1 = self_play(&r1, &o1);
    h2 = self_play(&r2, &o2);
    CHECK(h1 == h2 && r1 == r2 && o1 == o2, "self-play replay identical");
    CHECK(o1 != OUT_NONE, "self-play reaches an outcome (round %u)", r1);
    printf("self-play: outcome %u after round %u, state hash %08X, draws %u\n", o1, r1, h1, (unsigned)B.rng.draws);
    printf("battle/ai: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
