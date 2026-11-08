/* Host tests: appendix D fixed vectors + random cross-check against tools/ref_model.py */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/rng.h"
#include "../../src/core/rules.h"
#include "../../src/core/path.h"

extern uint8_t host_terrain[4096], host_occ[4096];

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails < 30) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void set_plain(uint8_t w, uint8_t h)
{
    grid_w = w; grid_h = h;
    memset(host_terrain, 0, sizeof host_terrain);
    memset(host_occ, 0, sizeof host_occ);
}

static void fixed_vectors(void)
{
    uint16_t tr[7], p[32];
    PhysIn in;
    Rng r;
    MoveRange mr;
    uint8_t n, i;
    static const uint32_t xs[5] = {270369ul, 67634689ul, 2647435461ul, 307599695ul, 2398689233ul};

    /* physical_full: hit 90%, intermediates 100,120,150,187,149,149,149 */
    CHECK(rules_phys_hit(40, 30, 100, 2) == 90, "hit90");   /* forest: penalty 10 */
    in.atk = 100; in.def = 80; in.hp = 600; in.max_hp = 600;
    in.fam_a = FAM_CAVALRY; in.fam_d = FAM_INFANTRY;          /* 125% */
    in.morale = 100; in.terrain_d = 2; in.rain = 0; in.variance = 100;   /* forest DR20 */
    CHECK(rules_phys_damage(&in, tr) == 149, "dmg149 got %u", tr[6]);
    {
        static const uint16_t e[7] = {100, 120, 150, 187, 149, 149, 149};
        CHECK(memcmp(tr, e, sizeof e) == 0, "trace full");
    }
    /* hit boundary: roll 90 misses when hit% = 90 */
    CHECK(!(90 < rules_phys_hit(40, 30, 100, 2)), "miss at 90");
    /* injured_floor_chain */
    in.hp = 300; in.variance = 90;
    {
        static const uint16_t e[7] = {75, 70, 87, 108, 86, 86, 77};
        CHECK(rules_phys_damage(&in, tr) == 77, "dmg77");
        CHECK(memcmp(tr, e, sizeof e) == 0, "trace injured");
    }
    /* signed floor */
    CHECK(rules_phys_hit(29, 30, 100, 2) == 84, "hit84 got %u", rules_phys_hit(29, 30, 100, 2));
    /* minimum damage */
    in.atk = 4; in.def = 200; in.hp = 600; in.max_hp = 600; in.fam_a = FAM_BOW; in.fam_d = FAM_INFANTRY;
    in.morale = 0; in.terrain_d = 2; in.rain = 1; in.variance = 90;
    CHECK(rules_phys_damage(&in, NULL) == 1, "min1");
    /* fire on forest */
    CHECK(rules_mag_hit(90, 60, 80) == 93, "maghit93");
    CHECK(rules_mag_damage(40, 80, 60, 80, rules_element_percent(FX_FIRE, 2, 0), 100, tr) == 128, "fire128");
    /* xorshift32 */
    rng_seed(&r, 1);
    for (i = 0; i < 5; i++) CHECK(rng_next(&r) == xs[i], "xorshift %u", i);
    CHECK(r.draws == 5, "draw count");
    /* paths */
    set_plain(3, 3);
    path_move_range(&mr, 0, 0, 9, MT_FOOT, SIDE_A);
    CHECK(path_dist(&mr, 2, 2) == 4, "cost4");
    n = path_backtrack(&mr, 2, 2, p, 32);
    CHECK(n == 5 && p[0] == 0 && p[1] == 1 && p[2] == 2 && p[3] == 5 && p[4] == 8, "path tie");
    host_terrain[1] = 8;   /* wall */
    path_move_range(&mr, 0, 0, 9, MT_FOOT, SIDE_A);
    n = path_backtrack(&mr, 2, 0, p, 32);
    CHECK(n == 5 && p[0] == 0 && p[1] == 3 && p[2] == 4 && p[3] == 5 && p[4] == 2, "path block");
    /* budget reject */
    path_move_range(&mr, 0, 0, 3, MT_FOOT, SIDE_A);
    CHECK(path_dist(&mr, 2, 0) == MR_INF, "budget reject");
    /* fixture maxHP rev2 */
    {
        StatIn s; Derived d;
        static const uint8_t cl[3] = {0, 6, 3};
        static const uint16_t hp[3] = {300, 320, 280}, mp[3] = {20, 18, 20};
        memset(&s, 0, sizeof s);
        for (i = 0; i < 3; i++) {
            s.cls = cl[i]; s.level = 1; s.str = 70; s.intl = 60; s.cmd = 70;
            rules_derive(&s, &d);
            CHECK(d.max_hp == hp[i] && d.max_mp == mp[i], "fixture %u: %u %u", i, d.max_hp, d.max_mp);
        }
        /* ttk table 4/5/5/5/5 */
        {
            static const uint8_t lv[5] = {1, 10, 30, 60, 99}, hits[5] = {4, 5, 5, 5, 5};
            for (i = 0; i < 5; i++) {
                uint16_t dmg;
                s.cls = 0; s.level = lv[i]; s.str = 70; s.cmd = 70; s.intl = 60;
                rules_derive(&s, &d);
                in.atk = d.atk; in.def = d.def; in.hp = d.max_hp; in.max_hp = d.max_hp;
                in.fam_a = FAM_INFANTRY; in.fam_d = FAM_INFANTRY; in.morale = 100;
                in.terrain_d = 0; in.rain = 0; in.variance = 100;
                dmg = rules_phys_damage(&in, NULL);
                CHECK((d.max_hp + dmg - 1) / dmg == hits[i], "ttk L%u", lv[i]);
            }
        }
    }
    /* xp / level */
    {
        uint8_t L = 98, xp = 90;
        CHECK(rules_xp_hit(10, 12, 1, 1) == 34, "xp kill");
        CHECK(rules_xp_hit(10, 40, 1, 0) == 30, "xp cap");
        CHECK(rules_xp_hit(10, 1, 0, 0) == 1, "xp miss");
        CHECK(rules_add_xp(&L, &xp, 250) == 1 && L == 99 && xp == 0, "L99 cap");
        L = 5; xp = 50;
        CHECK(rules_add_xp(&L, &xp, 260) == 3 && L == 8 && xp == 10, "multi level");
        CHECK(rules_keep_deficit(300, 250, 312, 1) == 262, "deficit");
        CHECK(rules_percent_ceil(441, 10) == 45, "ceil10");
    }
}

static int read_ints(char *s, long *v, int max)
{
    int n = 0;
    char *tok = strtok(s, " \n");
    while (tok && n < max) { v[n++] = strtol(tok, NULL, 10); tok = strtok(NULL, " \n"); }
    return n;
}

static long A[9000], E[9000];

static void random_cases(const char *path)
{
    FILE *f = fopen(path, "r");
    static char line[200000];
    MoveRange mr;
    if (!f) { printf("cannot open %s\n", path); fails++; return; }
    while (fgets(line, sizeof line, f)) {
        char kind[16];
        char *eq = strchr(line, '=');
        char *sp = strchr(line, ' ');
        int na = 0, ne = 0, i;
        if (!sp) continue;
        memcpy(kind, line, (size_t)(sp - line)); kind[sp - line] = 0;
        if (strcmp(kind, "GRID") == 0) {
            char *occ = strstr(line, " OCC ");
            *occ = 0;
            na = read_ints(sp + 1, A, 9000);
            grid_w = (uint8_t)A[0]; grid_h = (uint8_t)A[1];
            for (i = 0; i < grid_w * grid_h; i++) host_terrain[i] = (uint8_t)A[2 + i];
            ne = read_ints(occ + 5, E, 9000);
            for (i = 0; i < ne; i++) host_occ[i] = (uint8_t)E[i];
            continue;
        }
        if (!eq) continue;
        *eq = 0;
        na = read_ints(sp + 1, A, 9000);
        ne = read_ints(eq + 1, E, 9000);
        if (!strcmp(kind, "PHIT")) {
            CHECK(rules_phys_hit((uint8_t)A[0], (uint8_t)A[1], (uint8_t)A[2], (uint8_t)A[3]) == E[0], "PHIT");
        } else if (!strcmp(kind, "PDMG")) {
            PhysIn in; uint16_t tr[7], d;
            in.atk = (uint16_t)A[0]; in.def = (uint16_t)A[1]; in.hp = (uint16_t)A[2]; in.max_hp = (uint16_t)A[3];
            in.fam_a = (uint8_t)A[4]; in.fam_d = (uint8_t)A[5]; in.morale = (uint8_t)A[6]; in.terrain_d = (uint8_t)A[7];
            in.rain = (uint8_t)A[8]; in.variance = (uint8_t)A[9];
            d = rules_phys_damage(&in, tr);
            CHECK(d == E[0], "PDMG %u vs %ld", d, E[0]);
            for (i = 0; i < 7; i++) CHECK(tr[i] == E[1 + i], "PDMG trace %d", i);
        } else if (!strcmp(kind, "MHIT")) {
            CHECK(rules_mag_hit((uint8_t)A[0], (uint8_t)A[1], (uint8_t)A[2]) == E[0], "MHIT");
        } else if (!strcmp(kind, "MDMG")) {
            uint16_t tr[4], d = rules_mag_damage((uint8_t)A[0], (uint16_t)A[1], (uint16_t)A[2], (uint8_t)A[3], (uint8_t)A[4], (uint8_t)A[5], tr);
            CHECK(d == E[0], "MDMG");
            for (i = 0; i < 4; i++) CHECK(tr[i] == E[1 + i], "MDMG trace");
        } else if (!strcmp(kind, "DERIVE")) {
            StatIn s; Derived d;
            memset(&s, 0, sizeof s);
            s.cls = (uint8_t)A[0]; s.level = (uint8_t)A[1]; s.str = (uint8_t)A[2]; s.intl = (uint8_t)A[3]; s.cmd = (uint8_t)A[4];
            rules_derive(&s, &d);
            CHECK(d.max_hp == E[0] && d.max_mp == E[1] && d.atk == E[2] && d.def == E[3] && d.mag == E[4]
                  && d.res == E[5] && d.agi == E[6] && d.move == E[7], "DERIVE");
        } else if (!strcmp(kind, "RNG")) {
            Rng r; rng_seed(&r, (uint32_t)A[0]);
            for (i = 0; i < 5; i++) CHECK(rng_next(&r) == (uint32_t)E[i], "RNG seed %ld", A[0]);
        } else if (!strcmp(kind, "MOVE")) {
            int x, y;
            path_move_range(&mr, (uint8_t)A[0], (uint8_t)A[1], (uint8_t)A[2], (uint8_t)A[3], (uint8_t)A[4]);
            for (y = 0; y < grid_h; y++) for (x = 0; x < grid_w; x++)
                CHECK(path_dist(&mr, (uint8_t)x, (uint8_t)y) == E[y * grid_w + x], "MOVE dist (%d,%d)", x, y);
        } else if (!strcmp(kind, "PATH")) {
            uint16_t p[32];
            uint8_t n = path_backtrack(&mr, (uint8_t)A[0], (uint8_t)A[1], p, 32);
            CHECK(n == E[0], "PATH len %u vs %ld", n, E[0]);
            for (i = 0; i < n && i < E[0]; i++) CHECK(p[i] == E[1 + i], "PATH step");
        } else if (!strcmp(kind, "FIELD")) {
            FieldJob j; uint16_t t[8]; int k;
            for (k = 0; k < A[2]; k++) t[k] = (uint16_t)A[3 + k];
            field_begin(&j, (uint8_t)A[0], (uint8_t)A[1], t, (uint8_t)A[2]);
            while (!field_step(&j, 64)) {}
            for (k = 0; k < grid_w * grid_h; k++) CHECK(fld_get((uint16_t)k) == E[k], "FIELD cell %d %u vs %ld", k, fld_get((uint16_t)k), E[k]);
        } else if (!strcmp(kind, "LOS")) {
            CHECK(los_clear((uint8_t)A[0], (uint8_t)A[1], (uint8_t)A[2], (uint8_t)A[3]) == E[0], "LOS (%ld,%ld)-(%ld,%ld)", A[0], A[1], A[2], A[3]);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    fixed_vectors();
    printf("fixed vectors: %d checks, %d failures\n", checks, fails);
    if (argc > 1) random_cases(argv[1]);
    printf("total: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
