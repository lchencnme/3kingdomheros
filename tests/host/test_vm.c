/* Event VM host tests (design doc 9): ordering, once, conditions, IF skipping, SPAWN placement and
 * slot rules, RETREAT/HP_SET/OUTCOME, bad opcode, budget. Scripts are hand-assembled here. */
#include <stdio.h>
#include <string.h>
#include "../../src/core/vm.h"
#include "../../src/core/grid.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- platform hooks for the test */
static uint8_t blob[1024];
static VmEvent evs[16];
static uint8_t n_ev;
static uint8_t flags_b[512], flags_g[256];
static uint16_t texts[32];
static uint8_t speakers[32], n_text;
static uint16_t gold;
static VmTemplate tpls[4];

uint8_t vm_event_count(void) PLATFORM_API { return n_ev; }
void vm_event_header(uint8_t i, VmEvent *e) PLATFORM_API { *e = evs[i]; }
void vm_read(uint16_t off, uint8_t *dst, uint8_t n) PLATFORM_API { memcpy(dst, blob + off, n); }
uint8_t vm_flag_get(uint8_t s, uint16_t id) PLATFORM_API { uint8_t *f = s ? flags_b : flags_g; return (f[id >> 3] >> (id & 7)) & 1; }
void vm_flag_set(uint8_t s, uint16_t id, uint8_t v) PLATFORM_API { uint8_t *f = s ? flags_b : flags_g; if (v) f[id >> 3] |= (uint8_t)(1u << (id & 7)); else f[id >> 3] &= (uint8_t)~(1u << (id & 7)); }
void vm_text(uint8_t sp, uint16_t id) PLATFORM_API { speakers[n_text] = sp; texts[n_text++] = id; }
uint8_t vm_template(uint8_t i, VmTemplate *t) PLATFORM_API { if (i >= 4) return 0; *t = tpls[i]; return 1; }
void vm_gold(uint16_t a) PLATFORM_API { gold += a; }
static uint8_t duels;
void vm_duel(uint8_t w, uint8_t l, uint8_t r) PLATFORM_API { (void)w; (void)l; (void)r; duels++; }

static Battle B;
static uint16_t at;
static void emit(const uint8_t *p, uint16_t n) { memcpy(blob + at, p, n); at += n; }
#define E(...) do { const uint8_t _b[] = {__VA_ARGS__}; emit(_b, sizeof _b); } while (0)

static uint8_t ev(uint8_t trig, uint8_t param, uint8_t prio, uint16_t once, uint16_t cond, uint16_t code, uint16_t len)
{
    VmEvent *e = &evs[n_ev];
    e->trigger = trig; e->param = param; e->priority = prio; e->reserved = 0;
    e->once_bit = once; e->cond_off = cond; e->code_off = code; e->code_len = len;
    return n_ev++;
}

static void reset(void)
{
    uint8_t i;
    grid_w = 10; grid_h = 10;
    memset(host_terrain, 0, sizeof host_terrain); memset(host_occ, 0, sizeof host_occ);
    memset(host_units, 0, sizeof host_units);
    for (i = 0; i < MAX_UNITS; i++) { host_base[i].str = 70; host_base[i].intl = 60; host_base[i].cmd = 70; }
    memset(&B, 0, sizeof B); B.round = 1; B.turn_limit = 30; B.leader = 0;
    memset(flags_b, 0, sizeof flags_b); memset(flags_g, 0, sizeof flags_g);
    n_ev = 0; at = 0; n_text = 0; gold = 0;
    battle_spawn(&B, 0, 1, 0, FAC_PLAYER, 1, 1, 1);
    battle_spawn(&B, 1, 0xFFFF, 0, FAC_ENEMY, 1, 8, 8);
    tpls[0].character = 0xFFFF; tpls[0].cls = 0; tpls[0].level = 1; tpls[0].faction = FAC_ENEMY; tpls[0].instance_id = 5;
    tpls[1] = tpls[0]; tpls[1].instance_id = 0;   /* bad: slot already used */
}

int main(void)
{
    VmFact f;
    uint16_t c0, c1, c2;
    memset(&f, 0, sizeof f);

    /* ordering: priority asc then index asc; once runs once; texts recorded in order */
    reset();
    c0 = at; E(OP_TEXT, 0, 10, 0, OP_END);
    c1 = at; E(OP_TEXT, 1, 20, 0, OP_END);
    c2 = at; E(OP_TEXT, 2, 30, 0, OP_END);
    ev(TR_BATTLE_START, VM_ANY, 5, 0, 0xFFFF, c1, 5);    /* prio 5 */
    ev(TR_BATTLE_START, VM_ANY, 1, 1, 0xFFFF, c0, 5);    /* prio 1 -> first */
    ev(TR_BATTLE_START, VM_ANY, 5, VM_NO_ONCE, 0xFFFF, c2, 5);   /* prio 5, later index */
    f.trigger = TR_BATTLE_START;
    CHECK(vm_fire(&B, &f) == VM_OK && n_text == 3, "ran 3");
    CHECK(texts[0] == 10 && texts[1] == 20 && texts[2] == 30, "priority then id order: %u %u %u", texts[0], texts[1], texts[2]);
    n_text = 0;
    vm_fire(&B, &f);
    CHECK(n_text == 1 && texts[0] == 30, "once events do not rerun; non-once reruns on a new fact");

    /* conditions: turn_cmp, unit_alive, unit_in_rect, all/any/not, actor/param matching */
    reset();
    {
        uint16_t cnd = at;
        E(CN_ALL, 2, CN_TURN_CMP, CMP_GE, 3, 0, CN_NOT, CN_UNIT_IN_RECT, 0, 5, 5, 9, 9);
        c0 = at; E(OP_TEXT, 0xFF, 7, 0, OP_END);
        ev(TR_PHASE_START, FAC_ENEMY, 0, VM_NO_ONCE, cnd, c0, 5);
        f.trigger = TR_PHASE_START; f.phase = FAC_ENEMY;
        vm_fire(&B, &f);
        CHECK(n_text == 0, "turn 1 < 3: no run");
        B.round = 3; vm_fire(&B, &f);
        CHECK(n_text == 1, "turn 3 and unit 0 outside rect: run");
        f.phase = FAC_PLAYER; vm_fire(&B, &f);
        CHECK(n_text == 1, "param phase must match");
        f.phase = FAC_ENEMY; battle_move(0, 6, 6); vm_fire(&B, &f);
        CHECK(n_text == 1, "unit 0 inside rect: NOT fails");
    }
    /* IF skip + SET_FLAG + FLAG_EQ + GIVE_GOLD */
    reset();
    {
        uint16_t cnd = at;
        E(CN_FLAG_EQ, SCOPE_GLOBAL, 7, 0, 1);
        c0 = at;
        E(OP_IF, (uint8_t)cnd, 0, 3, 0, OP_GIVE_GOLD, 100, 0);   /* if flag7 == 1 then gold += 100 */
        E(OP_SET_FLAG, SCOPE_GLOBAL, 7, 0, 1);
        E(OP_END);
        ev(TR_ROUND_END, VM_ANY, 0, VM_NO_ONCE, 0xFFFF, c0, (uint16_t)(at - c0));
        f.trigger = TR_ROUND_END;
        vm_fire(&B, &f);
        CHECK(gold == 0 && vm_flag_get(SCOPE_GLOBAL, 7), "IF false skipped the gift, flag set afterwards");
        vm_fire(&B, &f);
        CHECK(gold == 100, "IF true runs the gift (%u)", gold);
    }
    /* SPAWN: occupied target cell -> nearest free by distance then (y,x); slot reuse is BAD_SCRIPT */
    reset();
    c0 = at; E(OP_SPAWN, 0, 8, 8, OP_END);      /* (8,8) has the enemy */
    ev(TR_ROUND_END, VM_ANY, 0, 0, 0xFFFF, c0, 5);
    f.trigger = TR_ROUND_END;
    CHECK(vm_fire(&B, &f) == VM_OK, "spawn ok");
    CHECK(B.n_units == 6 && host_units[5].max_hp == 300 && host_units[5].x == 8 && host_units[5].y == 7,
          "spawned at nearest (y,x)-first free cell: (%u,%u)", host_units[5].x, host_units[5].y);
    CHECK(host_occ[7 * 10 + 8] == SIDE_E, "spawn occupies its cell");
    reset();
    c0 = at; E(OP_SPAWN, 1, 3, 3, OP_END);
    ev(TR_ROUND_END, VM_ANY, 0, VM_NO_ONCE, 0xFFFF, c0, 5);
    CHECK(vm_fire(&B, &f) == VM_BAD_SCRIPT, "spawn into a used slot is BAD_SCRIPT");
    /* RETREAT / HP_SET / OUTCOME stop further events */
    reset();
    c0 = at; E(OP_RETREAT, 1, OP_END);
    c1 = at; E(OP_TEXT, 0, 1, 0, OP_END);
    ev(TR_ROUND_END, VM_ANY, 0, VM_NO_ONCE, 0xFFFF, c0, 3);
    ev(TR_ROUND_END, VM_ANY, 1, VM_NO_ONCE, 0xFFFF, c1, 5);
    vm_fire(&B, &f);
    CHECK(host_units[1].hp == 0 && host_occ[8 * 10 + 8] == 0, "RETREAT clears hp and occupancy");
    CHECK(B.outcome == OUT_WIN && n_text == 0, "all enemies removed -> win seals; later events do not run");
    reset();
    c0 = at; E(OP_HP_SET, 0, 0x10, 0x27, OP_END);   /* 10000 -> clamped to max */
    ev(TR_ROUND_END, VM_ANY, 0, VM_NO_ONCE, 0xFFFF, c0, 5);
    host_units[0].hp = 10;
    vm_fire(&B, &f);
    CHECK(host_units[0].hp == host_units[0].max_hp, "HP_SET clamps to max");
    /* bad opcode */
    reset();
    c0 = at; E(0x7E, OP_END);
    ev(TR_ROUND_END, VM_ANY, 0, VM_NO_ONCE, 0xFFFF, c0, 2);
    CHECK(vm_fire(&B, &f) == VM_BAD_SCRIPT, "unknown opcode -> BAD_SCRIPT");
    /* TALK availability (would_fire) + FACTION_SET recruit; DUEL replaces the action with 0 draws */
    reset();
    battle_spawn(&B, 2, 0xFFFF, 3, FAC_ENEMY, 1, 2, 1);           /* enemy archer next to unit 0 */
    {
        uint16_t cnd = at;
        E(CN_TARGET_EQ, 2);
        c0 = at; E(OP_FACTION_SET, 2, FAC_PLAYER, OP_END);
        ev(TR_TALK, 0, 0, 0, cnd, c0, 4);
        f.trigger = TR_TALK; f.actor = 0; f.target = 1;
        CHECK(vm_would_fire(&B, &f) == 0, "talk to another unit: no event");
        f.target = 2;
        CHECK(vm_would_fire(&B, &f) == 1, "talk to the archer is available");
        f.actor = 1;
        CHECK(vm_would_fire(&B, &f) == 0, "only the scripted actor can talk");
        f.actor = 0;
        vm_fire(&B, &f);
        CHECK(host_units[2].faction == FAC_PLAYER && host_occ[1 * 10 + 2] == SIDE_A && vm_units_changed, "recruited: faction + occupancy side");
        CHECK(host_units[2].action_flags & AF_ACTED, "recruit cannot act again this phase");
        CHECK(vm_would_fire(&B, &f) == 0, "once talk no longer available");
    }
    reset();
    {
        uint16_t cnd = at;
        uint32_t d0 = B.rng.draws;
        E(CN_TARGET_EQ, 1);
        c0 = at; E(OP_TEXT, 1, 3, 0, OP_DUEL, 0, 1, 0, 0, OP_END);
        ev(TR_PRE_ACTION, 0, 0, 0, cnd, c0, 10);
        duels = 0;
        f.trigger = TR_PRE_ACTION; f.actor = 0; f.target = 1;
        vm_fire(&B, &f);
        CHECK(vm_action_replaced && duels == 1, "duel replaced the attack and was presented");
        CHECK(host_units[1].hp == 0 && (host_units[1].action_flags & AF_RETREAT) && host_occ[8 * 10 + 8] == 0, "duel loser retreated");
        CHECK(host_units[0].action_flags & AF_ACTED, "duel consumes the winner's action");
        CHECK(B.rng.draws == d0, "duel draws no RNG");
        CHECK(B.outcome == OUT_WIN, "last enemy removed by duel -> win");
    }
    printf("vm: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
