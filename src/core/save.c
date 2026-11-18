#ifdef __SDCC
#pragma bank 4
#endif
#include <string.h>
#include "sram_map.h"
#include "save.h"
#include "grid.h"
#include "../gen/rules_data.h"

/* ---------------------------------------------------------------- CRC32 ISO-HDLC (reflected EDB88320) */





/* byte-sliced CRC tables t0..t3, contiguous (the GB copies all 1024 B to page-aligned WRAM) */
const uint8_t crc_t0[1024] = {
0, 150, 44, 186, 25, 143, 53, 163, 50, 164, 30, 136, 43, 189, 7, 145,
    100, 242, 72, 222, 125, 235, 81, 199, 86, 192, 122, 236, 79, 217, 99, 245,
    200, 94, 228, 114, 209, 71, 253, 107, 250, 108, 214, 64, 227, 117, 207, 89,
    172, 58, 128, 22, 181, 35, 153, 15, 158, 8, 178, 36, 135, 17, 171, 61,
    144, 6, 188, 42, 137, 31, 165, 51, 162, 52, 142, 24, 187, 45, 151, 1,
    244, 98, 216, 78, 237, 123, 193, 87, 198, 80, 234, 124, 223, 73, 243, 101,
    88, 206, 116, 226, 65, 215, 109, 251, 106, 252, 70, 208, 115, 229, 95, 201,
    60, 170, 16, 134, 37, 179, 9, 159, 14, 152, 34, 180, 23, 129, 59, 173,
    32, 182, 12, 154, 57, 175, 21, 131, 18, 132, 62, 168, 11, 157, 39, 177,
    68, 210, 104, 254, 93, 203, 113, 231, 118, 224, 90, 204, 111, 249, 67, 213,
    232, 126, 196, 82, 241, 103, 221, 75, 218, 76, 246, 96, 195, 85, 239, 121,
    140, 26, 160, 54, 149, 3, 185, 47, 190, 40, 146, 4, 167, 49, 139, 29,
    176, 38, 156, 10, 169, 63, 133, 19, 130, 20, 174, 56, 155, 13, 183, 33,
    212, 66, 248, 110, 205, 91, 225, 119, 230, 112, 202, 92, 255, 105, 211, 69,
    120, 238, 84, 194, 97, 247, 77, 219, 74, 220, 102, 240, 83, 197, 127, 233,
    28, 138, 48, 166, 5, 147, 41, 191, 46, 184, 2, 148, 55, 161, 27, 141,
0, 48, 97, 81, 196, 244, 165, 149, 136, 184, 233, 217, 76, 124, 45, 29,
    16, 32, 113, 65, 212, 228, 181, 133, 152, 168, 249, 201, 92, 108, 61, 13,
    32, 16, 65, 113, 228, 212, 133, 181, 168, 152, 201, 249, 108, 92, 13, 61,
    48, 0, 81, 97, 244, 196, 149, 165, 184, 136, 217, 233, 124, 76, 29, 45,
    65, 113, 32, 16, 133, 181, 228, 212, 201, 249, 168, 152, 13, 61, 108, 92,
    81, 97, 48, 0, 149, 165, 244, 196, 217, 233, 184, 136, 29, 45, 124, 76,
    97, 81, 0, 48, 165, 149, 196, 244, 233, 217, 136, 184, 45, 29, 76, 124,
    113, 65, 16, 32, 181, 133, 212, 228, 249, 201, 152, 168, 61, 13, 92, 108,
    131, 179, 226, 210, 71, 119, 38, 22, 11, 59, 106, 90, 207, 255, 174, 158,
    147, 163, 242, 194, 87, 103, 54, 6, 27, 43, 122, 74, 223, 239, 190, 142,
    163, 147, 194, 242, 103, 87, 6, 54, 43, 27, 74, 122, 239, 223, 142, 190,
    179, 131, 210, 226, 119, 71, 22, 38, 59, 11, 90, 106, 255, 207, 158, 174,
    194, 242, 163, 147, 6, 54, 103, 87, 74, 122, 43, 27, 142, 190, 239, 223,
    210, 226, 179, 131, 22, 38, 119, 71, 90, 106, 59, 11, 158, 174, 255, 207,
    226, 210, 131, 179, 38, 22, 71, 119, 106, 90, 11, 59, 174, 158, 207, 255,
    242, 194, 147, 163, 54, 6, 87, 103, 122, 74, 27, 43, 190, 142, 223, 239,
0, 7, 14, 9, 109, 106, 99, 100, 219, 220, 213, 210, 182, 177, 184, 191,
    183, 176, 185, 190, 218, 221, 212, 211, 108, 107, 98, 101, 1, 6, 15, 8,
    110, 105, 96, 103, 3, 4, 13, 10, 181, 178, 187, 188, 216, 223, 214, 209,
    217, 222, 215, 208, 180, 179, 186, 189, 2, 5, 12, 11, 111, 104, 97, 102,
    220, 219, 210, 213, 177, 182, 191, 184, 7, 0, 9, 14, 106, 109, 100, 99,
    107, 108, 101, 98, 6, 1, 8, 15, 176, 183, 190, 185, 221, 218, 211, 212,
    178, 181, 188, 187, 223, 216, 209, 214, 105, 110, 103, 96, 4, 3, 10, 13,
    5, 2, 11, 12, 104, 111, 102, 97, 222, 217, 208, 215, 179, 180, 189, 186,
    184, 191, 182, 177, 213, 210, 219, 220, 99, 100, 109, 106, 14, 9, 0, 7,
    15, 8, 1, 6, 98, 101, 108, 107, 212, 211, 218, 221, 185, 190, 183, 176,
    214, 209, 216, 223, 187, 188, 181, 178, 13, 10, 3, 4, 96, 103, 110, 105,
    97, 102, 111, 104, 12, 11, 2, 5, 186, 189, 180, 179, 215, 208, 217, 222,
    100, 99, 106, 109, 9, 14, 7, 0, 191, 184, 177, 182, 210, 213, 220, 219,
    211, 212, 221, 218, 190, 185, 176, 183, 8, 15, 6, 1, 101, 98, 107, 108,
    10, 13, 4, 3, 103, 96, 105, 110, 209, 214, 223, 216, 188, 187, 178, 181,
    189, 186, 179, 180, 208, 215, 222, 217, 102, 97, 104, 111, 11, 12, 5, 2,
0, 119, 238, 153, 7, 112, 233, 158, 14, 121, 224, 151, 9, 126, 231, 144,
    29, 106, 243, 132, 26, 109, 244, 131, 19, 100, 253, 138, 20, 99, 250, 141,
    59, 76, 213, 162, 60, 75, 210, 165, 53, 66, 219, 172, 50, 69, 220, 171,
    38, 81, 200, 191, 33, 86, 207, 184, 40, 95, 198, 177, 47, 88, 193, 182,
    118, 1, 152, 239, 113, 6, 159, 232, 120, 15, 150, 225, 127, 8, 145, 230,
    107, 28, 133, 242, 108, 27, 130, 245, 101, 18, 139, 252, 98, 21, 140, 251,
    77, 58, 163, 212, 74, 61, 164, 211, 67, 52, 173, 218, 68, 51, 170, 221,
    80, 39, 190, 201, 87, 32, 185, 206, 94, 41, 176, 199, 89, 46, 183, 192,
    237, 154, 3, 116, 234, 157, 4, 115, 227, 148, 13, 122, 228, 147, 10, 125,
    240, 135, 30, 105, 247, 128, 25, 110, 254, 137, 16, 103, 249, 142, 23, 96,
    214, 161, 56, 79, 209, 166, 63, 72, 216, 175, 54, 65, 223, 168, 49, 70,
    203, 188, 37, 82, 204, 187, 34, 85, 197, 178, 43, 92, 194, 181, 44, 91,
    155, 236, 117, 2, 156, 235, 114, 5, 149, 226, 123, 12, 146, 229, 124, 11,
    134, 241, 104, 31, 129, 246, 111, 24, 136, 255, 102, 17, 143, 248, 97, 22,
    160, 215, 78, 57, 167, 208, 73, 62, 174, 217, 64, 55, 169, 222, 71, 48,
    189, 202, 83, 36, 186, 205, 84, 35, 179, 196, 93, 42, 180, 195, 90, 45};
#define crc_t1 (crc_t0 + 256)
#define crc_t2 (crc_t0 + 512)
#define crc_t3 (crc_t0 + 768)

/* byte-sliced table CRC: no 32-bit shifts on the SM83; u8 loop counter so SDCC keeps it in a register */
static uint8_t s0, s1, s2, s3;

static void crc_block(const uint8_t *p, uint8_t n)
{
    uint8_t c0 = s0, c1 = s1, c2 = s2, c3 = s3;
    do {
        uint8_t i = (uint8_t)(c0 ^ *p++);
        c0 = (uint8_t)(crc_t0[i] ^ c1);
        c1 = (uint8_t)(crc_t1[i] ^ c2);
        c2 = (uint8_t)(crc_t2[i] ^ c3);
        c3 = crc_t3[i];
    } while (--n);
    s0 = c0; s1 = c1; s2 = c2; s3 = c3;
}

#ifdef __SDCC
/* GB: SM83 assembly inner loop with WRAM tables (src/gb/crc_gb.c); this C loop is the host reference */
uint32_t crc_update_gb(uint32_t crc, const uint8_t *p, uint16_t n) NONBANKED;
void crc_prepare(void) NONBANKED;
#define CRC_PREPARE() crc_prepare()
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint16_t n) CORE_API
{
    return crc_update_gb(crc, p, n);
}
#else
#define CRC_PREPARE() ((void)0)
uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint16_t n) CORE_API
{
    s0 = (uint8_t)crc; s1 = (uint8_t)(crc >> 8); s2 = (uint8_t)(crc >> 16); s3 = (uint8_t)(crc >> 24);
    while (n) {
        uint8_t k = n > 255u ? 255u : (uint8_t)n;
        crc_block(p, k);
        p += k; n -= k;
    }
    return (uint32_t)s0 | ((uint32_t)s1 << 8) | ((uint32_t)s2 << 16) | ((uint32_t)s3 << 24);
}
#endif

/* ---------------------------------------------------------------- image addressing */
/* sram_map.h: camp (slots 0/1 share it: slot 1 is never written) and snapshot copies 2 KiB each in bank 0,
 * suspend copies 8 KiB in banks 1 and 2. An image is addressed by its 15-bit SRAM address. */
static uint16_t slot_bank(uint8_t slot, uint8_t copy)
{
    if (slot == SAVE_SLOT_SNAPSHOT) return copy ? SRAM_SNAP_B : SRAM_SNAP_A;
    if (slot == SAVE_SLOT_SUSPEND) return copy ? SRAM_SUSP_B : SRAM_SUSP_A;
    return copy ? SRAM_CAMP_B : SRAM_CAMP_A;
}
/* bytes one copy may hold (header + body) */
static uint16_t copy_cap(uint16_t base) { return base >= SRAM_SUSP_A ? SRAM_BIG_COPY : SRAM_SMALL_COPY; }

static void img_write(uint16_t base, uint16_t off, const uint8_t *src, uint16_t n)
{
    uint16_t a = (uint16_t)(base + off);
    while (n) {
        uint16_t room = (uint16_t)(0x2000u - (a & 0x1FFFu)), k = n < room ? n : room;
        sram_write((uint8_t)(a >> 13), (uint16_t)(a & 0x1FFFu), src, k);
        a += k; src += k; n -= k;
    }
}

static void img_read(uint16_t base, uint16_t off, uint8_t *dst, uint16_t n)
{
    uint16_t a = (uint16_t)(base + off);
    while (n) {
        uint16_t room = (uint16_t)(0x2000u - (a & 0x1FFFu)), k = n < room ? n : room;
        sram_read((uint8_t)(a >> 13), (uint16_t)(a & 0x1FFFu), dst, k);
        a += k; dst += k; n -= k;
    }
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

/* ---------------------------------------------------------------- streaming writer */
typedef struct {
    uint16_t base;
    uint16_t pos;              /* body offset */
    uint16_t cap;              /* body bytes this copy holds */
    uint8_t over;              /* the body did not fit: the save fails, nothing past the copy is written */
    uint32_t crc;
} Writer;

static void w_bytes(Writer *w, const uint8_t *p, uint16_t n)
{
    if (w->over || (uint32_t)w->pos + n > w->cap) { w->over = 1; w->pos += n; return; }
    img_write(w->base, (uint16_t)(32u + w->pos), p, n);
    w->crc = crc32_update(w->crc, p, n);
    w->pos += n;
}

static void w_fill(Writer *w, uint8_t v, uint16_t n)
{
    uint8_t buf[32];
    memset(buf, v, sizeof buf);
    while (n) { uint16_t k = n < 32u ? n : 32u; w_bytes(w, buf, k); n -= k; }
}

static void w_tlv(Writer *w, uint8_t tag, uint16_t len)
{
    uint8_t h[3];
    h[0] = tag; put16(h + 1, len);
    w_bytes(w, h, 3);
}

/* core512 is written as ordered (offset, field) pieces with zero gaps (附录B.3) */
typedef struct { Writer *w; uint16_t at; } CoreW;
static void c_seek(CoreW *c, uint16_t off) { if (off > c->at) { w_fill(c->w, 0, (uint16_t)(off - c->at)); c->at = off; } }
static void c_u8(CoreW *c, uint16_t off, uint8_t v) { c_seek(c, off); w_bytes(c->w, &v, 1); c->at++; }
static void c_u16(CoreW *c, uint16_t off, uint16_t v) { uint8_t b[2]; put16(b, v); c_seek(c, off); w_bytes(c->w, b, 2); c->at += 2; }
static void c_u32(CoreW *c, uint16_t off, uint32_t v) { uint8_t b[4]; put32(b, v); c_seek(c, off); w_bytes(c->w, b, 4); c->at += 4; }

static const uint8_t battle_tags[9] = {1, 2, 3, 4, 5, 6, 7, 9, 10};
static const uint8_t camp_tags[4] = {1, 2, 3, 7};
static const uint8_t snap_tags[5] = {1, 2, 3, 7, 8};
static const uint8_t *snap_dep;           /* tag 8 source while a snapshot is written: n + n x 4 B */

static uint8_t probe_copy(uint16_t base, uint32_t *seq, uint16_t *blen, uint8_t *kind);

/* probe cache (design doc 11.6 performance): SRAM is only written by this program, so the result of
 * the last full probe / successful save of a slot stays true; save_battle uses it to pick the target
 * copy without re-reading and re-CRCing both copies. Loads always run the full validation. */
static uint8_t pc_state[4];       /* 0 unknown, 1 known: newest valid copy in pc_copy, 2 known: none */
static uint8_t pc_copy[4];
static uint8_t pc_kind[4];        /* SAVE_KIND_* of the newest valid copy */
static uint32_t pc_seq[4];

void save_cache_reset(void) CORE_API { pc_state[0] = pc_state[1] = pc_state[2] = pc_state[3] = 0; }

uint8_t save_probe(uint8_t slot, uint8_t *copy, uint32_t *seq) CORE_API
{
    uint32_t sa = 0, sb = 0;
    CRC_PREPARE();
    uint16_t la, lb;
    uint8_t ka = 0, kb = 0;
    uint8_t va = probe_copy(slot_bank(slot, 0), &sa, &la, &ka) == SAVE_OK;
    uint8_t vb = probe_copy(slot_bank(slot, 1), &sb, &lb, &kb) == SAVE_OK;
    pc_state[slot] = 2;
    if (va && vb) {
        uint32_t d = sa - sb;               /* modular comparison (11.6) */
        if (d == 0x80000000ul) { pc_state[slot] = 0; return SAVE_ERR_CONFLICT; }   /* refuse to guess */
        if (d == 0 || d < 0x80000000ul) { *copy = 0; *seq = sa; } else { *copy = 1; *seq = sb; }
    } else if (va) { *copy = 0; *seq = sa; }
    else if (vb) { *copy = 1; *seq = sb; }
    else return SAVE_ERR_NONE;
    pc_state[slot] = 1; pc_copy[slot] = *copy; pc_seq[slot] = *seq; pc_kind[slot] = *copy ? kb : ka;
    return SAVE_OK;
}

uint8_t save_kind(uint8_t slot) CORE_API { return pc_state[slot] == 1 ? pc_kind[slot] : 0xFFu; }

/* copy n bytes of an SRAM area into the body in 32 B pieces */
static void w_sram(Writer *w, uint8_t bank, uint16_t off, uint16_t n)
{
    uint8_t fb[32];
    while (n) { uint16_t k = n < 32u ? n : 32u; sram_read(bank, off, fb, (uint8_t)k); w_bytes(w, fb, k); off += k; n -= k; }
}

static void w_campaign(Writer *w)
{
    uint8_t fb[32];
    uint16_t k;
    for (k = 0; k < 256u; k += 32u) { campaign_read((uint8_t)k, fb, 32); w_bytes(w, fb, 32); }
}

static void w_core(Writer *w, const Battle *b, const SaveUi *ui, uint8_t kind)
{
    CoreW c;
    uint8_t pq[17], k;
    w_tlv(w, 7, 512);
    c.w = w; c.at = 0;
    c_u16(&c, 0, ui->battle_id); c_u16(&c, 2, b->round); c_u8(&c, 4, b->phase);
    c_u16(&c, 6, RULES_PROFILE_REVISION); c_u32(&c, 8, b->rng.state); c_u32(&c, 12, b->rng.draws);
    c_u8(&c, 23, b->leader); c_u8(&c, 26, b->outcome);
    c_u16(&c, 32, ui->cam_x); c_u16(&c, 34, ui->cam_y); c_u8(&c, 36, ui->cur_x); c_u8(&c, 37, ui->cur_y);
    c_u8(&c, 39, ui->presentation); c_u8(&c, 40, ui->text_speed); c_u8(&c, 42, ui->options);
    c_u8(&c, 43, kind); c_u32(&c, 44, ui->gold); c_u32(&c, 52, RULES_PROFILE_CRC);
    sram_read(RT_BANK, RT_PRIZE_OFF, pq, 17);                       /* prize queue (7.4): count @71, 16 x u16 @432 */
    if (pq[0] > 16u) pq[0] = 0;
    c_u8(&c, 71, pq[0]);
    c_u8(&c, 80, b->weather);
    for (k = 0; k < 16u; k++) c_u16(&c, (uint16_t)(432u + 2u * k), k < pq[0] ? pq[1 + k] : 0xFFFFu);
    c_seek(&c, 512);
}

static uint8_t save_write(uint8_t slot, const Battle *b, const SaveUi *ui, uint8_t kind)
{
    Writer w;
    uint8_t copy, target, i, hdr[32], zero2[2] = {0, 0}, buf[40], nr;
    uint32_t seq = 0, crc;
    ActiveUnit u;
    CRC_PREPARE();
    if (pc_state[slot] == 1) { copy = pc_copy[slot]; seq = pc_seq[slot]; target = (uint8_t)(copy ^ 1u); seq++; }
    else if (pc_state[slot] == 2) { target = 0; seq = 1; }
    else if (save_probe(slot, &copy, &seq) == SAVE_OK) { target = (uint8_t)(copy ^ 1u); seq++; }
    else { target = 0; seq = 1; }
    pc_state[slot] = 0;                                 /* unknown until this save completes */
    w.base = slot_bank(slot, target); w.pos = 0; w.crc = 0xFFFFFFFFul; w.over = 0;
    w.cap = (uint16_t)(copy_cap(w.base) - 32u);

    img_write(w.base, 30, zero2, 2);                    /* 1. clear both markers of the target copy */
    /* 2. body (TLV, increasing tags) */
    sram_read(RT_BANK, RT_ROSTER_COUNT, &nr, 1);                       /* roster (5.6 / B.2): count + records */
    if (nr > RT_ROSTER_MAX) nr = 0;
    w_tlv(&w, 1, (uint16_t)(1u + (uint16_t)nr * 48u)); w_bytes(&w, &nr, 1);
    w_sram(&w, RT_BANK, RT_ROSTER_OFF, (uint16_t)nr * 48u);
    w_tlv(&w, 2, 256); w_sram(&w, RT_BANK, RT_NAMED_OFF, 256);      /* named states */
    w_tlv(&w, 3, 256); w_campaign(&w);                  /* campaign area (global flags) */
    if (kind == SAVE_KIND_BATTLE) {
        w_tlv(&w, 4, (uint16_t)(1u + (uint16_t)b->n_units * 40u));
        buf[0] = b->n_units; w_bytes(&w, buf, 1);
        for (i = 0; i < b->n_units; i++) {              /* field-by-field little endian = struct layout B.1 */
            unit_load(i, &u);
            memcpy(buf, &u, 40);
            w_bytes(&w, buf, 40);
        }
        w_tlv(&w, 5, 512); w_sram(&w, RT_BANK, RT_FLAGS_OFF, 512);  /* battle flags */
        w_tlv(&w, 6, 2); w_fill(&w, 0, 2);              /* terrain diffs: count 0 */
    }
    w_core(&w, b, ui, kind);
    if (kind == SAVE_KIND_SNAPSHOT) {                   /* tag 8: deployment (2.3) */
        w_tlv(&w, 8, (uint16_t)(1u + (uint16_t)snap_dep[0] * 4u));
        w_bytes(&w, snap_dep, (uint16_t)(1u + (uint16_t)snap_dep[0] * 4u));
    }
    if (kind == SAVE_KIND_BATTLE) {
        w_tlv(&w, 9, RT_INV_UNITS * 20u); w_sram(&w, RT_BANK, RT_INV_OFF, RT_INV_UNITS * 20u);   /* unit items (7.1) */
        w_tlv(&w, 10, 960); w_sram(&w, RT_BANK, RT_AI_GROUPS, 960); /* AI groups + overrides (8.2) */
    }
    if (w.over) return SAVE_ERR_IO;                    /* too big for its copy: markers stay cleared, old copy valid */
    crc = w.crc ^ 0xFFFFFFFFul;
    /* bytes after body_len are not written (not covered by any CRC; readers never look at them) */
    /* 3. header 0..29 */
    memset(hdr, 0, sizeof hdr);
    hdr[0] = 'Y'; hdr[1] = 'J'; hdr[2] = '2'; hdr[3] = '0';
    put16(hdr + 4, SAVE_SCHEMA); put16(hdr + 6, w.pos); put32(hdr + 8, seq);
    put32(hdr + 12, RULES_PROFILE_CRC); put32(hdr + 16, crc);
    put32(hdr + 20, crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul);
    img_write(w.base, 0, hdr, 30);
    /* 4. readback CRC of body and header */
    {
#define rb buf                                          /* reuse the 40 B unit buffer: the stack is 512 B */
        uint32_t rc = 0xFFFFFFFFul;
        uint16_t off = 0;
        img_read(w.base, 0, rb, 30);
        if (memcmp(rb, hdr, 30) != 0) return SAVE_ERR_IO;
        while (off < w.pos) {
            uint16_t k = (uint16_t)(w.pos - off) < 32u ? (uint16_t)(w.pos - off) : 32u;
            img_read(w.base, (uint16_t)(32u + off), rb, k);
            rc = crc32_update(rc, rb, k);
            off += k;
        }
        if ((rc ^ 0xFFFFFFFFul) != crc) return SAVE_ERR_IO;
#undef rb
    }
    /* 5. inverse marker, 6. commit marker last */
    hdr[31] = 0x5A; img_write(w.base, 31, &hdr[31], 1);
    hdr[30] = 0xA5; img_write(w.base, 30, &hdr[30], 1);
    {
        uint8_t m[2];
        img_read(w.base, 30, m, 2);
        if (m[0] != 0xA5 || m[1] != 0x5A) return SAVE_ERR_IO;
    }
    pc_state[slot] = 1; pc_copy[slot] = target; pc_seq[slot] = seq; pc_kind[slot] = kind;
    return SAVE_OK;
}

uint8_t save_battle(uint8_t slot, const Battle *b, const SaveUi *ui) CORE_API { return save_write(slot, b, ui, SAVE_KIND_BATTLE); }
static uint16_t tlv_find(uint16_t base, uint8_t tag);
static void load_common(uint16_t base, Battle *b, SaveUi *ui);

uint8_t save_snapshot(const Battle *b, const SaveUi *ui, const uint8_t *dep) CORE_API
{
    uint8_t r;
    snap_dep = dep;
    r = save_write(SAVE_SLOT_SNAPSHOT, b, ui, SAVE_KIND_SNAPSHOT);
    snap_dep = 0;
    return r;
}

uint8_t load_snapshot(Battle *b, SaveUi *ui, uint8_t *dep) CORE_API
{
    uint8_t copy, n;
    uint16_t base;
    uint32_t seq;
    uint16_t at;
    uint8_t r = save_probe(SAVE_SLOT_SNAPSHOT, &copy, &seq);
    if (r != SAVE_OK) return r;
    if (pc_kind[SAVE_SLOT_SNAPSHOT] != SAVE_KIND_SNAPSHOT) return SAVE_ERR_NONE;
    base = slot_bank(SAVE_SLOT_SNAPSHOT, copy);
    load_common(base, b, ui);
    at = tlv_find(base, 8);
    img_read(base, at, &n, 1);
    if (n > SAVE_MAX_DEPLOY) n = SAVE_MAX_DEPLOY;      /* the caller buffer holds 1 + 4 x SAVE_MAX_DEPLOY */
    img_read(base, at, dep, (uint16_t)(1u + (uint16_t)n * 4u));
    dep[0] = n;
    return SAVE_OK;
}

uint8_t save_camp(uint8_t slot, const SaveUi *ui) CORE_API
{
    Battle b;
    memset(&b, 0, sizeof b);
    return save_write(slot, &b, ui, SAVE_KIND_CAMP);
}

/* full structural validation of one copy (header, CRCs, TLV set/lengths); no state is touched */
static uint8_t probe_copy(uint16_t base, uint32_t *seq, uint16_t *blen, uint8_t *kind)
{
    uint8_t seen[10];
    uint8_t hdr[32], t = 0;                  /* hdr doubles as the CRC buffer: the stack is 512 B */
    uint16_t off = 0, len;
    uint32_t crc = 0xFFFFFFFFul, body_crc, sq;
    img_read(base, 0, hdr, 32);
    if (hdr[0] != 'Y' || hdr[1] != 'J' || hdr[2] != '2' || hdr[3] != '0') return SAVE_ERR_NONE;
    if (hdr[30] != 0xA5 || hdr[31] != 0x5A) return SAVE_ERR_NONE;
    { uint8_t k; for (k = 24; k < 30; k++) if (hdr[k]) return SAVE_ERR_NONE; }
    if ((crc32_update(0xFFFFFFFFul, hdr, 20) ^ 0xFFFFFFFFul) != get32(hdr + 20)) return SAVE_ERR_NONE;
    if (get16(hdr + 4) != SAVE_SCHEMA) return SAVE_ERR_SCHEMA;
    if (get32(hdr + 12) != RULES_PROFILE_CRC) return SAVE_ERR_PROFILE;
    len = get16(hdr + 6);
    if (len > SAVE_MAX_BODY || len > (uint16_t)(copy_cap(base) - 32u)) return SAVE_ERR_NONE;
    body_crc = get32(hdr + 16); sq = get32(hdr + 8);
    while (off < len) {
        uint16_t k = (uint16_t)(len - off) < 32u ? (uint16_t)(len - off) : 32u;
        img_read(base, (uint16_t)(32u + off), hdr, k);
        crc = crc32_update(crc, hdr, k);
        off += k;
    }
    if ((crc ^ 0xFFFFFFFFul) != body_crc) return SAVE_ERR_NONE;
    /* TLV structure: exactly the battle or the camp tag set, increasing, exact lengths, inside body */
    off = 0;
    while (off < len) {
        uint8_t th[3], n;
        uint16_t tl, want;
        if ((uint16_t)(len - off) < 3u || t >= sizeof seen) return SAVE_ERR_NONE;
        img_read(base, (uint16_t)(32u + off), th, 3);
        tl = get16(th + 1);
        switch (th[0]) {
        case 1: case 4: case 8:
            img_read(base, (uint16_t)(32u + off + 3u), &n, 1);
            if (n > (th[0] == 1 ? 128u : MAX_UNITS)) return SAVE_ERR_NONE;
            want = (uint16_t)(1u + (uint16_t)n * (th[0] == 1 ? 48u : (th[0] == 4 ? 40u : 4u)));
            break;
        case 2: case 3: want = 256; break;
        case 5: case 7: want = 512; break;
        case 6: want = 2; break;
        case 9: want = RT_INV_UNITS * 20u; break;
        case 10: want = 960; break;
        default: return SAVE_ERR_NONE;
        }
        if (tl != want || (uint32_t)off + 3u + tl > len) return SAVE_ERR_NONE;
        seen[t++] = th[0];
        off = (uint16_t)(off + 3u + tl);
    }
    if (t == sizeof battle_tags && memcmp(seen, battle_tags, sizeof battle_tags) == 0) *kind = SAVE_KIND_BATTLE;
    else if (t == sizeof camp_tags && memcmp(seen, camp_tags, sizeof camp_tags) == 0) *kind = SAVE_KIND_CAMP;
    else if (t == sizeof snap_tags && memcmp(seen, snap_tags, sizeof snap_tags) == 0) *kind = SAVE_KIND_SNAPSHOT;
    else return SAVE_ERR_NONE;
    *seq = sq;
    *blen = len;
    return SAVE_OK;
}

/* body offset of a tag's payload (probe already validated the structure), or 0xFFFF */
static uint16_t tlv_find(uint16_t base, uint8_t tag)
{
    uint8_t hdr[8], th[3];
    uint16_t off = 0, len;
    img_read(base, 0, hdr, 8);
    len = get16(hdr + 6);
    while (off + 3u <= len) {
        img_read(base, (uint16_t)(32u + off), th, 3);
        if (th[0] == tag) return (uint16_t)(32u + off + 3u);
        off = (uint16_t)(off + 3u + get16(th + 1));
    }
    return 0xFFFFu;
}

static void r_sram(uint16_t base, uint16_t at, uint8_t bank, uint16_t off, uint16_t n)
{
    uint8_t fb[32];
    while (n) { uint16_t k = n < 32u ? n : 32u; img_read(base, at, fb, k); sram_write(bank, off, fb, (uint8_t)k); at += k; off += k; n -= k; }
}

/* tags shared by every save kind: roster, named states, campaign area, core */
static void load_common(uint16_t base, Battle *b, SaveUi *ui)
{
    uint8_t core[48], n, fb[32];
    uint16_t at = tlv_find(base, 1), k;
    img_read(base, at, &n, 1);
    if (n > RT_ROSTER_MAX) n = 0;
    sram_write(RT_BANK, RT_ROSTER_COUNT, &n, 1);
    r_sram(base, (uint16_t)(at + 1u), RT_BANK, RT_ROSTER_OFF, (uint16_t)n * 48u);
    r_sram(base, tlv_find(base, 2), RT_BANK, RT_NAMED_OFF, 256);
    at = tlv_find(base, 3);
    for (k = 0; k < 256u; k += 32u) { img_read(base, (uint16_t)(at + k), fb, 32); campaign_write((uint8_t)k, fb, 32); }
    at = tlv_find(base, 7);
    img_read(base, at, core, sizeof core);
    memset(b, 0, sizeof *b);
    ui->battle_id = get16(core + 0); b->round = get16(core + 2); b->phase = core[4];
    b->rng.state = get32(core + 8); b->rng.draws = get32(core + 12);
    b->leader = core[23]; b->outcome = core[26];
    ui->cam_x = get16(core + 32); ui->cam_y = get16(core + 34); ui->cur_x = core[36]; ui->cur_y = core[37];
    ui->presentation = core[39]; ui->text_speed = core[40]; ui->options = core[42]; ui->gold = get32(core + 44);
    img_read(base, (uint16_t)(at + 80u), &b->weather, 1);
    {                                                    /* prize queue: count @71, 16 x u16 @432 */
        uint8_t pq[17], raw[32];
        img_read(base, (uint16_t)(at + 71u), pq, 1);
        img_read(base, (uint16_t)(at + 432u), raw, 32);
        if (pq[0] > 16u) pq[0] = 0;
        for (k = 0; k < 16u; k++) pq[1 + k] = raw[2 * k];
        sram_write(RT_BANK, RT_PRIZE_OFF, pq, 17);
    }
}

uint8_t load_battle(uint8_t slot, Battle *b, SaveUi *ui) CORE_API
{
    uint8_t copy, n, i;
    uint16_t base;
    uint32_t seq;
    uint16_t at;
    ActiveUnit u;
    uint8_t r = save_probe(slot, &copy, &seq);
    if (r != SAVE_OK) return r;
    if (pc_kind[slot] != SAVE_KIND_BATTLE) return SAVE_ERR_NONE;
    base = slot_bank(slot, copy);
    load_common(base, b, ui);
    at = tlv_find(base, 4);
    img_read(base, at, &n, 1);
    for (i = 0; i < n; i++) {
        img_read(base, (uint16_t)(at + 1u + (uint16_t)i * 40u), (uint8_t *)&u, 40);
        unit_store(i, &u);
        if (unit_alive(&u)) grid_occ_set(u.x, u.y, faction_side(u.faction));
    }
    b->n_units = n;
    r_sram(base, tlv_find(base, 5), RT_BANK, RT_FLAGS_OFF, 512);     /* battle flags */
    r_sram(base, tlv_find(base, 9), RT_BANK, RT_INV_OFF, RT_INV_UNITS * 20u);   /* unit items */
    r_sram(base, tlv_find(base, 10), RT_BANK, RT_AI_GROUPS, 960);    /* AI groups + overrides */
    return SAVE_OK;
}

uint16_t save_peek_battle(uint8_t slot) CORE_API
{
    uint8_t copy, v[2];
    uint32_t seq;
    if (save_probe(slot, &copy, &seq) != SAVE_OK) return 0xFFFFu;
    img_read(slot_bank(slot, copy), tlv_find(slot_bank(slot, copy), 7), v, 2);
    return get16(v);
}

uint8_t load_camp(uint8_t slot, SaveUi *ui) CORE_API
{
    uint8_t copy;
    uint32_t seq;
    Battle b;
    uint8_t r = save_probe(slot, &copy, &seq);
    if (r != SAVE_OK) return r;
    if (pc_kind[slot] != SAVE_KIND_CAMP) return SAVE_ERR_NONE;
    load_common(slot_bank(slot, copy), &b, ui);
    return SAVE_OK;
}

void save_invalidate(uint8_t slot) CORE_API
{
    uint8_t z[2] = {0, 0}, c;
    pc_state[slot] = 2;
    for (c = 0; c < 2; c++) img_write(slot_bank(slot, c), 30, z, 2);
}
