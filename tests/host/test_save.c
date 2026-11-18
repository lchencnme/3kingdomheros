/* Save format tests (design doc 11.6): round trip, every-byte power cut, CRC/reserved/schema/profile
 * rejection, sequence wrap and half-range conflict. */
#include <stdio.h>
#include <string.h>
#include "../../src/core/save.h"
#include "../../src/core/battle.h"
#include "../../src/core/grid.h"

extern uint8_t host_terrain[4096], host_occ[4096];
extern ActiveUnit host_units[MAX_UNITS];
extern UnitBase host_base[MAX_UNITS];
extern uint8_t host_sram[4][8192];
extern long host_cut_after, host_writes;

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; if (fails < 20) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static Battle B;

static void setup(uint32_t seed)
{
    uint8_t i;
    grid_w = 16; grid_h = 16;
    memset(host_terrain, 0, sizeof host_terrain);
    memset(host_occ, 0, sizeof host_occ);
    memset(host_units, 0, sizeof host_units);
    for (i = 0; i < MAX_UNITS; i++) { host_base[i].str = 70; host_base[i].intl = 60; host_base[i].cmd = 70; }
    memset(&B, 0, sizeof B);
    rng_seed(&B.rng, seed);
    B.round = 3; B.turn_limit = 30; B.phase = FAC_PLAYER;
    for (i = 0; i < 7; i++) battle_spawn(&B, i, (uint16_t)(i + 1), (uint8_t)(i % 4 == 3 ? 3 : 0),
                                         i < 3 ? FAC_PLAYER : FAC_ENEMY, (uint8_t)(1 + i), (uint8_t)(2 + i), (uint8_t)(3 + (seed & 7)));
    host_units[4].hp = (uint16_t)(100 + seed % 50);
    rng_next(&B.rng); rng_next(&B.rng);
}

typedef struct { ActiveUnit u[7]; Battle b; SaveUi ui; } Snap;

static void snap(Snap *s, const SaveUi *ui)
{
    memcpy(s->u, host_units, sizeof s->u);
    s->b = B;
    s->ui = *ui;
}

static int same_as(const Snap *s)
{
    Battle b; SaveUi ui;
    memset(host_units, 0, sizeof host_units);
    memset(host_occ, 0, sizeof host_occ);
    if (load_battle(SAVE_SLOT_SUSPEND, &b, &ui) != SAVE_OK) return 0;
    if (memcmp(host_units, s->u, sizeof s->u) != 0) return 0;
    if (b.round != s->b.round || b.rng.state != s->b.rng.state || b.rng.draws != s->b.rng.draws
        || b.n_units != s->b.n_units || b.phase != s->b.phase || b.leader != s->b.leader) return 0;
    if (ui.cam_x != s->ui.cam_x || ui.cur_x != s->ui.cur_x || ui.cur_y != s->ui.cur_y) return 0;
    return 1;
}

int main(void)
{
    SaveUi ui = {0xFFFEu, 64, 32, 5, 11, 0, 1, 1, 1234};
    Snap s1, s2, s3;
    uint8_t copy;
    uint32_t seq;
    long total, k;

    memset(host_sram, 0, sizeof host_sram);
    CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_ERR_NONE, "empty sram has no save");

    /* round trip */
    setup(1); snap(&s1, &ui);
    CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "save ok");
    CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0 && seq == 1, "first save: copy A seq 1");
    CHECK(same_as(&s1), "round trip restores units/battle/ui");
    /* occupancy rebuilt from units */
    CHECK(host_occ[(uint16_t)host_units[0].y * grid_w + host_units[0].x] == SIDE_A, "occupancy rebuilt");

    /* second save goes to copy B */
    setup(2); ui.cur_x = 7; snap(&s2, &ui);
    host_writes = 0;
    CHECK(save_battle(SAVE_SLOT_SUSPEND, &B, &ui) == SAVE_OK, "save 2");
    total = host_writes;
    CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 1 && seq == 2, "second save: copy B seq 2");
    CHECK(same_as(&s2), "loads newest");
    printf("one save = %ld byte writes\n", total);

    /* power cut at every byte of a third save (targets copy A): result must be exactly s2 or s3 */
    setup(3); ui.cur_x = 9; snap(&s3, &ui);
    {
        static uint8_t base_img[16][8192];
        long old_ok = 0, new_ok = 0, bad = 0;
        memcpy(base_img, host_sram, sizeof host_sram);
        for (k = 0; k <= total + 1; k++) {
            int ok2, ok3;
            memcpy(host_sram, base_img, sizeof host_sram);
            save_cache_reset();
            setup(3);
            host_cut_after = k; host_writes = 0;
            save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
            host_cut_after = -1;
            ok3 = same_as(&s3);
            ok2 = !ok3 && same_as(&s2);
            if (ok3) new_ok++; else if (ok2) old_ok++; else bad++;
        }
        CHECK(bad == 0, "every power cut recovers old or new complete state (bad=%ld)", bad);
        CHECK(new_ok >= 1 && old_ok >= total - 2, "old kept until commit (old %ld new %ld)", old_ok, new_ok);
        printf("power-cut model: %ld cut points, old=%ld new=%ld bad=%ld\n", total + 2, old_ok, new_ok, bad);
        memcpy(host_sram, base_img, sizeof host_sram);
        save_cache_reset();
    }

    /* corruption rejection falls back to the other copy */
    {
        uint8_t keep;
        /* newest is copy B (bank 2); corrupt its body */
        keep = host_sram[2][100]; host_sram[2][100] ^= 0x55;
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0, "body CRC corrupt -> fall back to A");
        host_sram[2][100] = keep;
        keep = host_sram[2][9]; host_sram[2][9] ^= 1;                    /* header (sequence) */
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0, "header CRC corrupt -> A");
        host_sram[2][9] = keep;
        host_sram[2][25] = 1;                                             /* reserved byte */
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0, "reserved nonzero rejected");
        host_sram[2][25] = 0;
        host_sram[2][30] = 0;                                             /* commit marker missing */
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0, "uncommitted copy ignored");
        host_sram[2][30] = 0xA5;
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 1, "restored -> B newest again");
    }
    /* sequence wrap: A = 0xFFFFFFFF, B = 0 -> B newer; half-range -> conflict */
    {
        uint8_t hdr[32];
        uint32_t crc;
        int c;
        for (c = 0; c < 2; c++) {
            uint32_t s = c == 0 ? 0xFFFFFFFFul : 0u;
            uint8_t bank = (uint8_t)(1 + c);              /* suspend copies: banks 1 and 2 (sram_map.h) */
            memcpy(hdr, host_sram[bank], 32);
            hdr[8] = (uint8_t)s; hdr[9] = (uint8_t)(s >> 8); hdr[10] = (uint8_t)(s >> 16); hdr[11] = (uint8_t)(s >> 24);
            crc = crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul;
            hdr[20] = (uint8_t)crc; hdr[21] = (uint8_t)(crc >> 8); hdr[22] = (uint8_t)(crc >> 16); hdr[23] = (uint8_t)(crc >> 24);
            memcpy(host_sram[bank], hdr, 32);
        }
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 1 && seq == 0, "wrap: 0 newer than FFFFFFFF");
        memcpy(hdr, host_sram[2], 32);
        hdr[8] = 0; hdr[9] = 0; hdr[10] = 0; hdr[11] = 0x80;              /* B = 0x80000000, A = 0xFFFFFFFF -> delta 0x7FFFFFFF */
        crc = crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul;
        hdr[20] = (uint8_t)crc; hdr[21] = (uint8_t)(crc >> 8); hdr[22] = (uint8_t)(crc >> 16); hdr[23] = (uint8_t)(crc >> 24);
        memcpy(host_sram[2], hdr, 32);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK && copy == 0, "A newer by 0x7FFFFFFF");
        hdr[8] = 0xFF; hdr[9] = 0xFF; hdr[10] = 0xFF; hdr[11] = 0x7F;      /* B = 0x7FFFFFFF -> delta 0x80000000 */
        crc = crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul;
        hdr[20] = (uint8_t)crc; hdr[21] = (uint8_t)(crc >> 8); hdr[22] = (uint8_t)(crc >> 16); hdr[23] = (uint8_t)(crc >> 24);
        memcpy(host_sram[2], hdr, 32);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_ERR_CONFLICT, "half-range delta is a conflict");
    }
    /* schema / profile mismatch rejected (no migration) */
    {
        uint8_t hdr[32];
        uint32_t crc;
        memset(host_sram, 0, sizeof host_sram);
        save_cache_reset();
        setup(4);
        save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
        memcpy(hdr, host_sram[1], 32);
        hdr[4] = 9;                                                        /* unknown schema */
        crc = crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul;
        hdr[20] = (uint8_t)crc; hdr[21] = (uint8_t)(crc >> 8); hdr[22] = (uint8_t)(crc >> 16); hdr[23] = (uint8_t)(crc >> 24);
        memcpy(host_sram[1], hdr, 32);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) != SAVE_OK, "unknown schema rejected");
        memset(host_sram, 0, sizeof host_sram);
        save_cache_reset();
        save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
        memcpy(hdr, host_sram[1], 32);
        hdr[12] ^= 1;                                                      /* other rules profile */
        crc = crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul;
        hdr[20] = (uint8_t)crc; hdr[21] = (uint8_t)(crc >> 8); hdr[22] = (uint8_t)(crc >> 16); hdr[23] = (uint8_t)(crc >> 24);
        memcpy(host_sram[1], hdr, 32);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) != SAVE_OK, "other profile rejected");
        /* invalidate */
        memset(host_sram, 0, sizeof host_sram);
        save_cache_reset();
        save_battle(SAVE_SLOT_SUSPEND, &B, &ui);
        save_invalidate(SAVE_SLOT_SUSPEND);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_ERR_NONE, "invalidated suspend");
        /* slots are isolated */
        save_camp(SAVE_SLOT_NORMAL0, &ui);
        CHECK(save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_ERR_NONE && save_probe(SAVE_SLOT_NORMAL0, &copy, &seq) == SAVE_OK, "slot isolation");
        /* 32 KiB layout: a battle save does not fit a 2 KiB camp copy and must fail without writing past it */
        {
            uint8_t snap_b0 = host_sram[0][0x1000];
            CHECK(save_battle(SAVE_SLOT_NORMAL0, &B, &ui) == SAVE_ERR_IO && host_sram[0][0x1000] == snap_b0,
                  "oversized save refused, neighbour copy untouched");
        }
    }
    /* CRC32 check value: "123456789" -> CBF43926 */
    CHECK((crc32_update(0xFFFFFFFFul, (const uint8_t *)"123456789", 9) ^ 0xFFFFFFFFul) == 0xCBF43926ul, "CRC32 ISO-HDLC check value");
    printf("save: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
