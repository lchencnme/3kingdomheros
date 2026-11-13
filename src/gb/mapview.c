#pragma bank 11  /* map view compositor + TECH01 map data (direct reads, same bank; design doc 11.3) */
/* Scrolling map with the 352-position BG compositor (design doc 12.2).
 *
 * Every visible hardware position owns a private tile slot, so terrain and the static units standing
 * on it are composited into BG tiles (no OBJ per unit, no 10-per-line limit). Slot of world hardware
 * tile (wx, wy) = (wx mod 22) * 16 + (wy mod 16): a column of 16 positions is 16 consecutive slots,
 * i.e. one contiguous 256 B GDMA when the camera scrolls horizontally.
 *   slots   0..255 -> VRAM bank0 BG ids 0..255
 *   slots 256..303 -> VRAM bank1 ids 72..119   (columns 16..18)
 *   slots 304..351 -> VRAM bank1 ids 200..247  (columns 19..21)
 * (bank1 ids 0..71 and 128..199 are the two HUD pages.)
 * Units: 16x24, feet on their cell, top 8 px in the cell above; drawn by foot y then id; the tile takes
 * the top unit's palette and the terrain under it keeps only its ink (colour 3).
 * Palettes: 0..3 terrain, 4 player, 5 enemy, 6 move-range tint, 7 acted unit. */
#include <gb/gb.h>
#include <string.h>
#include "debug.h"
#include "mapview.h"
#include "wram.h"
#include "rom.h"
#include "../core/battle.h"
#include "../core/item.h"
#include "../gen/tech01_data.h"

uint16_t cam_x, cam_y;
static int16_t tox, toy;               /* loaded hw-tile origin = floor(cam/8)-1 */
static const uint8_t *meta;            /* [terrain][4] */
static const uint8_t *tpal;
static uint8_t hl_on, hl_row, hl_half; /* highlight repaint sweep: rows 0..15 x 2 half rows, 16 = idle */
static int16_t hb_x0, hb_x1, hb_y0, hb_y1;   /* hw-position box the sweep must recompose (old + new range) */

#define BLANK_ID_SLOT 0xFFFFu
#define CACHE_W 22
#define CACHE_H 16
#define PAL_PLAYER 4u
#define PAL_ENEMY 5u
#define PAL_HL 6u
#define PAL_ACTED 7u
#define MAXU 24u                       /* units drawn: all of a battle (units_gb SNAP_MAX) */
#define BODY_ACTED 0x80u     /* cur_body bit: unit has acted (solid foot mark) */

/* render state in WRAM bank7 D400..D47F (TileMeta follows at D480); the dirty queue in WRAM0 C3A0..C3DF */
typedef struct { uint8_t x, y, vis, pal, body; } RU;
#define RS_QMAX 32u                    /* overflow -> full-cache recomposition sweep */
typedef struct {
    RU ru[MAXU];                       /* what is currently drawn */
    uint8_t nq;
} RenderState;
#define RS ((RenderState *)0xD400u)
typedef char rs_fits[(sizeof(RenderState) <= 0x80u) ? 1 : -1];
__at(0xC3A0) uint8_t rs_qx[RS_QMAX];   /* dirty world hardware positions */
__at(0xC3C0) uint8_t rs_qy[RS_QMAX];

extern uint8_t snap_x[MAXU], snap_y[MAXU], snap_fac[MAXU], snap_flags[MAXU], snap_cls[MAXU];
extern uint16_t snap_hp[MAXU];
void units_snapshot(uint8_t n);
static uint8_t n_units_seen;
extern uint8_t unit_gen;
static uint8_t seen_gen, have_gen, diff_gen;
/* WRAM0 C370..C39F (audio region tail, after the unit snapshot; design doc 11.4) */
__at(0xC370) uint8_t order[MAXU];       /* visible units sorted by (foot y, id) */
__at(0xC388) uint8_t cand[MAXU];        /* subset of order that can touch the current edge / position */
static uint8_t n_order, n_cand;

/* restrict compose() to units whose 16x24 box can overlap hardware columns [wx0, wx1] x rows [wy0, wy1] */
static void filter_units(int16_t wx0, int16_t wx1, int16_t wy0, int16_t wy1)
{
    uint8_t k;
    n_cand = 0;
    for (k = 0; k < n_order; k++) {
        uint8_t u = order[k];
        int16_t ux = (int16_t)snap_x[u] * 2, uy = (int16_t)snap_y[u] * 2 - 1;
        if (ux + 1 < wx0 || ux > wx1 || uy + 2 < wy0 || uy > wy1) continue;
        cand[n_cand++] = u;
    }
}
__at(0xC328) uint8_t cur_vis[MAXU];
__at(0xC340) uint8_t cur_pal[MAXU];
__at(0xC358) uint8_t cur_body[MAXU];
#define MAX_TRE 4u
static uint8_t tre_x[MAX_TRE], tre_y[MAX_TRE], n_tre;   /* untaken treasure cells (drawn as chests) */

/* staging: C100 (256 B, design doc 11.4); pending transfers built in the visible period */
#define STAGE ((uint8_t *)0xC100u)
static uint8_t edge_kind;              /* 0 none, 1 column, 2 row (two halves) */
static int16_t edge_pos;
static uint8_t edge_half;
static uint8_t n_tiles;                /* staged tiles (16 B each) */
/* per staged tile, everything VBlank needs, precomputed in the visible period (WRAM bank7 D480..D4BF):
 * VBlank only copies. On SameBoy / hardware VRAM writes during mode 3 are dropped, so the commit must be
 * short and must stop when VBlank ends (12.1). */
typedef struct {
    uint8_t id[16], at[16];            /* BG tile id, attribute (bit3 = VRAM bank of the tile) */
    uint16_t moff[16];                 /* BG map offset ((wy & 31) << 5 | (wx & 31)) */
} TileMeta;
__at(0xD480) TileMeta mv_tmeta;           /* absolute (WRAM bank7 when SVBK = 7); no RAM allocated */
#define TM (&mv_tmeta)
typedef char tm_fits[(sizeof(TileMeta) <= 0x40u) ? 1 : -1];
static uint8_t n_done;                 /* staged tiles already committed (a commit may span VBlanks) */
static uint8_t q_overflow;             /* dirty queue overflowed: a full sweep follows */
static uint8_t contiguous;             /* staged tiles form one contiguous slot run */

static uint16_t max_cam_x(void) { uint16_t w = (uint16_t)grid_w * 16u; return w > VIEW_W ? w - VIEW_W : 0; }
static uint16_t max_cam_y(void) { uint16_t h = (uint16_t)grid_h * 16u; return h > VIEW_H ? h - VIEW_H : 0; }

/* mathematical mod without division (SM83 has none): wx in [-2, 130], m = 22; wy uses & 15 */
static uint8_t mod22(int16_t v) { while (v < 0) v += 22; while (v >= 22) v -= 22; return (uint8_t)v; }
static uint16_t slot_of(int16_t wx, int16_t wy) { return (uint16_t)mod22(wx) * 16u + (uint8_t)(wy & 15); }
static uint8_t slot_id(uint16_t s) { return s < 256u ? (uint8_t)s : (s < 304u ? (uint8_t)(72u + (s - 256u)) : (uint8_t)(200u + (s - 304u))); }
static uint8_t slot_bank(uint16_t s) { return s < 256u ? 0u : 1u; }
static uint16_t id_addr(uint8_t id) { return id < 128u ? (uint16_t)(0x9000u + (uint16_t)id * 16u) : (uint16_t)(0x8800u + (uint16_t)(id - 128u) * 16u); }

static uint8_t body_of(uint8_t cls)
{
    /* the fixed-bank class table is unbanked: direct read is fine */
    uint8_t f = class_defs[cls].family;
    return f == FAM_BOW ? UNIT_BODY_BOW : (f == FAM_CAVALRY ? UNIT_BODY_HORSE : UNIT_BODY_FOOT);
}

/* current unit visuals from the snapshot (alive, palette, body) and the draw order */
static void refresh_units(void)
{
    uint8_t i, k;
    if (have_gen && seen_gen == unit_gen) return;     /* no unit record written since the last refresh */
    seen_gen = unit_gen; have_gen = 1;
    units_snapshot(n_units_seen);
    n_order = 0;
    for (i = 0; i < n_units_seen && i < MAXU; i++) {
        uint8_t vis = snap_hp[i] && !(snap_flags[i] & (AF_RETREAT | AF_DEAD | AF_AWAY));
        cur_vis[i] = vis;
        cur_pal[i] = snap_fac[i] == FAC_ENEMY ? PAL_ENEMY : ((snap_flags[i] & AF_ACTED) ? PAL_ACTED : PAL_PLAYER);
        cur_body[i] = (uint8_t)(body_of(snap_cls[i]) | ((snap_flags[i] & AF_ACTED) ? BODY_ACTED : 0u));
        if (!vis) continue;
        for (k = n_order; k > 0 && (snap_y[order[k - 1]] > snap_y[i]); k--) order[k] = order[k - 1];
        order[k] = i;
        n_order++;
    }
}

/* 12.2 markers, ink only: foot mark on the feet row (hollow = can act, solid = acted) and a side mark on
 * the head tile (player square, ally circle, enemy triangle) - identification never relies on colour */
static void ink(uint8_t *d, uint8_t row, uint8_t bits) { d[2 * row] |= bits; d[2 * row + 1] |= bits; }
static void mark(uint8_t *d, uint8_t lx, uint8_t ly, uint8_t fac, uint8_t acted)
{
    if (ly == 2) {
        uint8_t line = lx ? 0xF8u : 0x1Fu, end = lx ? 0x08u : 0x10u;
        ink(d, 5, line); ink(d, 7, line);
        if (acted) ink(d, 6, line); else ink(d, 6, end);
    } else if (ly == 0 && lx == 0) {
        if (fac == FAC_ENEMY) { ink(d, 0, 0x80); ink(d, 1, 0xC0); ink(d, 2, 0xE0); }
        else if (fac == FAC_ALLY) { ink(d, 0, 0x40); ink(d, 1, 0xE0); ink(d, 2, 0x40); }
        else { ink(d, 0, 0xE0); ink(d, 1, 0xE0); ink(d, 2, 0xE0); }
    }
}

/* compose world hardware tile (wx, wy) into dst (16 B); returns its attribute (palette | bank bit) */
static uint8_t compose(int16_t wx, int16_t wy, uint8_t *dst, uint8_t hl, uint8_t dz)
{
    uint8_t pal = 0, k, y, top = 0xFF;
    if (wx < 0 || wy < 0 || wx >= (int16_t)grid_w * 2 || wy >= (int16_t)grid_h * 2) {
        memset(dst, 0, 16);
    } else {
        uint8_t cx = (uint8_t)(wx >> 1), cy = (uint8_t)(wy >> 1), t = grid_terrain(cx, cy) & 0x0F;
        uint8_t q = (uint8_t)(((wy & 1) << 1) | (wx & 1));
        memcpy(dst, &tech01_bg_tiles[(uint16_t)meta[t * 4 + q] * 16u], 16);   /* same ROM bank as this code */
        pal = hl ? PAL_HL : tpal[t];
        if (q == 3u) {     /* treasure chest in the cell's lower-right tile, under any unit (7.4) */
            uint8_t k;
            for (k = 0; k < n_tre; k++) if (tre_x[k] == cx && tre_y[k] == cy) {
                static const uint8_t chest[8] = {0x00, 0x00, 0x7E, 0x42, 0x7E, 0x5A, 0x7E, 0x00};
                for (y = 0; y < 8; y++) { dst[2 * y] |= chest[y]; dst[2 * y + 1] |= chest[y]; }
            }
        }
        if (dz) {          /* enemy danger view: an X at the cell centre, one arm per tile (12.2, 8.4) */
            static const uint8_t r0[4] = {5, 5, 0, 0};
            static const uint8_t xa[4][3] = {{0x04, 0x02, 0x01}, {0x20, 0x40, 0x80}, {0x01, 0x02, 0x04}, {0x80, 0x40, 0x20}};
            for (y = 0; y < 3; y++) { dst[2 * (r0[q] + y)] |= xa[q][y]; dst[2 * (r0[q] + y) + 1] |= xa[q][y]; }
        }
        if (hl) {          /* move range: ink corner ticks at the cell's four outer corners (12.2) */
            static const uint8_t row_a[4] = {0, 0, 7, 7}, row_b[4] = {1, 1, 6, 6}, row_c[4] = {2, 2, 5, 5};
            static const uint8_t bar[4] = {0xE0, 0x07, 0xE0, 0x07}, tick[4] = {0x80, 0x01, 0x80, 0x01};
            dst[2 * row_a[q]] |= bar[q]; dst[2 * row_a[q] + 1] |= bar[q];
            dst[2 * row_b[q]] |= tick[q]; dst[2 * row_b[q] + 1] |= tick[q];
            dst[2 * row_c[q]] |= tick[q]; dst[2 * row_c[q] + 1] |= tick[q];
        }
    }
    /* the top unit (last in draw order) gives the tile its palette (12.2) */
    for (k = 0; k < n_cand; k++) {
        uint8_t u = cand[k];
        int16_t lx = wx - (int16_t)snap_x[u] * 2, ly = wy - ((int16_t)snap_y[u] * 2 - 1);
        if (lx >= 0 && lx <= 1 && ly >= 0 && ly <= 2) top = u;
    }
    if (top != 0xFF) {     /* offline remap table: terrain colours -> nearest colours of that palette */
        uint8_t rm = tech01_remap[pal][cur_pal[top] - 4u], lsel = rm & 7u, hsel = (uint8_t)(rm >> 4);
        for (y = 0; y < 8; y++) {
            uint8_t lo = dst[2 * y], hi = dst[2 * y + 1];
            uint8_t m1 = (uint8_t)(lo & ~hi), m2 = (uint8_t)(~lo & hi), m3 = (uint8_t)(lo & hi);
            dst[2 * y] = (uint8_t)(((lsel & 1u) ? m1 : 0u) | ((lsel & 2u) ? m2 : 0u) | ((lsel & 4u) ? m3 : 0u));
            dst[2 * y + 1] = (uint8_t)(((hsel & 1u) ? m1 : 0u) | ((hsel & 2u) ? m2 : 0u) | ((hsel & 4u) ? m3 : 0u));
        }
    }
    /* units whose 16x24 box covers this position, in draw order (pre-filtered candidates) */
    for (k = 0; k < n_cand; k++) {
        uint8_t u = cand[k];
        int16_t lx = wx - (int16_t)snap_x[u] * 2, ly = wy - ((int16_t)snap_y[u] * 2 - 1);
        const uint8_t *ut;
        if (lx < 0 || lx > 1 || ly < 0 || ly > 2) continue;
        ut = &tech01_unit_tiles[((uint16_t)(cur_body[u] & 3u) * 6u + (uint16_t)ly * 2u + (uint16_t)lx) * UNIT_TILE_BYTES];
        for (y = 0; y < 8; y++) {
            uint8_t m = ut[16 + y], nm = (uint8_t)~m;
            dst[2 * y] = (uint8_t)((dst[2 * y] & nm) | ut[2 * y]);
            dst[2 * y + 1] = (uint8_t)((dst[2 * y + 1] & nm) | ut[2 * y + 1]);
        }
        mark(dst, (uint8_t)lx, (uint8_t)ly, snap_fac[u], (uint8_t)(cur_body[u] & BODY_ACTED));
        pal = cur_pal[u];
    }
    return pal;
}

static uint8_t dz_on;
static void load_treasures(void);
#define W7_DANGER ((uint8_t *)0xDC00u)   /* danger bitmap (overlays the AI scratch while the view is shown) */
static uint8_t in_danger(int16_t wx, int16_t wy)
{
    uint8_t r, o;
    uint16_t c;
    if (!dz_on || wx < 0 || wy < 0 || wx >= (int16_t)grid_w * 2 || wy >= (int16_t)grid_h * 2) return 0;
    c = (uint16_t)(wy >> 1) * grid_w + (uint16_t)(wx >> 1);
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    r = (uint8_t)((W7_DANGER[c >> 3] >> (c & 7u)) & 1u);
    SVBK_REG = o;
    return r;
}

static uint8_t in_range_hl(int16_t wx, int16_t wy)
{
    uint8_t r = 0, o;
    MoveRange *mr;
    int16_t lx, ly;
    if (!hl_on || wx < 0 || wy < 0) return 0;
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    mr = W7_MR;
    lx = (wx >> 1) - mr->bx; ly = (wy >> 1) - mr->by;
    if (lx >= 0 && ly >= 0 && lx < (int16_t)MR_DIM && ly < (int16_t)MR_DIM)
        r = mr->dist[(uint16_t)ly * MR_DIM + (uint16_t)lx] != MR_INF;
    SVBK_REG = o;
    return r;
}

static void stage_tile(int16_t wx, int16_t wy)
{
    uint16_t s = slot_of(wx, wy);
    uint8_t at = compose(wx, wy, STAGE + (uint16_t)n_tiles * 16u, in_range_hl(wx, wy), in_danger(wx, wy));
    uint8_t id = slot_id(s), o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    TM->id[n_tiles] = id;
    TM->at[n_tiles] = (uint8_t)(at | (slot_bank(s) ? 0x08u : 0u));
    TM->moff[n_tiles] = (uint16_t)(((uint16_t)(wy & 31) << 5) | (uint16_t)(wx & 31));
    SVBK_REG = o;
    n_tiles++;
}

static void write_map_byte(int16_t wx, int16_t wy, uint8_t v)
{
    *((uint8_t *)0x9800u + ((uint16_t)(wy & 31) << 5) + (wx & 31)) = v;
}

void mapview_init(const uint8_t *meta4, const uint8_t *terrain_pal) BANKED
{
    int16_t x, y;
    uint8_t buf[16], i;
    meta = meta4; tpal = terrain_pal;
    hl_on = 0; dz_on = 0; hl_row = CACHE_H; hl_half = 0; n_done = 0; q_overflow = 0; edge_kind = 0; n_tiles = 0;
    n_units_seen = MAXU;                       /* every record; empty ones (hp 0) are invisible */
    have_gen = 0;
    if (cam_x > max_cam_x()) cam_x = max_cam_x();
    if (cam_y > max_cam_y()) cam_y = max_cam_y();
    tox = (int16_t)(cam_x >> 3) - 1;
    toy = (int16_t)(cam_y >> 3) - 1;
    refresh_units();
    load_treasures();
    {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH;
        for (i = 0; i < MAXU; i++) { RS->ru[i].x = snap_x[i]; RS->ru[i].y = snap_y[i]; RS->ru[i].vis = cur_vis[i]; RS->ru[i].pal = cur_pal[i]; RS->ru[i].body = cur_body[i]; }
        RS->nq = 0;
        SVBK_REG = o;
        diff_gen = unit_gen;
    }
    /* LCD is off: compose all 352 positions straight into VRAM */
    filter_units(tox, tox + CACHE_W - 1, toy, toy + CACHE_H - 1);
    for (y = toy; y < toy + CACHE_H; y++)
        for (x = tox; x < tox + CACHE_W; x++) {
            uint16_t s = slot_of(x, y);
            uint8_t at = compose(x, y, buf, 0, 0);
            VBK_REG = slot_bank(s);
            set_bkg_data(slot_id(s), 1, buf);
            VBK_REG = VBK_TILES;
            write_map_byte(x, y, slot_id(s));
            VBK_REG = VBK_ATTRIBUTES;
            write_map_byte(x, y, (uint8_t)(at | (slot_bank(s) ? 0x08u : 0u)));
        }
    VBK_REG = VBK_TILES;
    SCX_REG = (uint8_t)cam_x;
    SCY_REG = (uint8_t)cam_y;
}

void mapview_set_units(uint8_t n) BANKED { n_units_seen = n > MAXU ? MAXU : n; }

static void queue_cell(uint8_t x, uint8_t y);
static void load_treasures(void)
{
    TreasureDef d;
    uint8_t i;
    n_tre = 0;
    for (i = 0; treasure_def(i, &d) && n_tre < MAX_TRE; i++)
        if (treasure_at(d.x, d.y) == i) { tre_x[n_tre] = d.x; tre_y[n_tre] = d.y; n_tre++; }
}

void mapview_treasures(void) BANKED
{
    uint8_t ox[MAX_TRE], oy[MAX_TRE], on = n_tre, k, o;
    for (k = 0; k < on; k++) { ox[k] = tre_x[k]; oy[k] = tre_y[k]; }
    load_treasures();
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    for (k = 0; k < on; k++) queue_cell(ox[k], oy[k]);       /* covers the chest tile (2x+1, 2y+1) */
    SVBK_REG = o;
}

void mapview_danger(uint8_t on) BANKED
{
    if (on == dz_on) return;
    dz_on = on;
    hb_x0 = tox; hb_x1 = tox + CACHE_W - 1; hb_y0 = toy; hb_y1 = toy + CACHE_H - 1;   /* whole cache */
    hl_row = 0; hl_half = 0;
}

void mapview_highlight(uint8_t on) BANKED
{
    /* the sweep recomposes the union of the previous and the current range boxes (MR box, cells) */
    int16_t x0, y0, o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    x0 = (int16_t)W7_MR->bx * 2; y0 = (int16_t)W7_MR->by * 2;
    SVBK_REG = (uint8_t)o;
    if (hl_row >= CACHE_H) { hb_x0 = x0; hb_y0 = y0; hb_x1 = x0 + 2 * MR_DIM - 1; hb_y1 = y0 + 2 * MR_DIM - 1; }
    else {
        if (x0 < hb_x0) hb_x0 = x0;
        if (y0 < hb_y0) hb_y0 = y0;
        if (x0 + 2 * MR_DIM - 1 > hb_x1) hb_x1 = x0 + 2 * MR_DIM - 1;
        if (y0 + 2 * MR_DIM - 1 > hb_y1) hb_y1 = y0 + 2 * MR_DIM - 1;
    }
    hl_on = on;
    hl_row = 0; hl_half = 0;
}

void mapview_camera_follow(uint8_t cx, uint8_t cy) BANKED
{
    int16_t px = (int16_t)cx * 16 + 8 - (int16_t)cam_x;
    int16_t py = (int16_t)cy * 16 + 8 - (int16_t)cam_y;
    int16_t tx = (int16_t)cam_x, ty = (int16_t)cam_y;
    int16_t ntx, nty;
    if (px < 32) tx = (int16_t)cx * 16 + 8 - 32;
    else if (px > 128) tx = (int16_t)cx * 16 + 8 - 128;
    if (py < 24) ty = (int16_t)cy * 16 + 8 - 24;
    else if (py > 88) ty = (int16_t)cy * 16 + 8 - 88;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx > (int16_t)max_cam_x()) tx = (int16_t)max_cam_x();
    if (ty > (int16_t)max_cam_y()) ty = (int16_t)max_cam_y();
    if (edge_kind || n_tiles) return;          /* previous transfer not committed: hold the camera */
    if (tx != (int16_t)cam_x) {
        int16_t d = tx - (int16_t)cam_x;
        if (d > 2) d = 2;
        if (d < -2) d = -2;
        ntx = (int16_t)cam_x + d;
        if ((ntx >> 3) - 1 != tox) {
            int16_t ntox = (ntx >> 3) - 1;
            uint8_t i;
            edge_kind = 1;
            edge_pos = ntox > tox ? ntox + CACHE_W - 1 : ntox;
            tox = ntox;
            refresh_units();
            filter_units(edge_pos, edge_pos, toy, toy + CACHE_H - 1);
            for (i = 0; i < CACHE_H; i++) stage_tile(edge_pos, toy + i);
            contiguous = (toy & 15) == 0;            /* column run is contiguous only if it starts at row slot 0 */
        }
        cam_x = (uint16_t)ntx;
    } else if (ty != (int16_t)cam_y) {
        int16_t d = ty - (int16_t)cam_y;
        if (d > 2) d = 2;
        if (d < -2) d = -2;
        nty = (int16_t)cam_y + d;
        if ((nty >> 3) - 1 != toy) {
            int16_t ntoy = (nty >> 3) - 1;
            uint8_t i;
            edge_kind = 2; edge_half = 0;
            edge_pos = ntoy > toy ? ntoy + CACHE_H - 1 : ntoy;
            toy = ntoy;
            refresh_units();
            filter_units(tox, tox + CACHE_W - 1, edge_pos, edge_pos);
            for (i = 0; i < 11; i++) stage_tile(tox + i, edge_pos);      /* first half of the row */
            contiguous = 0;
        }
        cam_y = (uint16_t)nty;
    }
}

static uint8_t RS_pending(void)
{
    uint8_t o = SVBK_REG, n;
    SVBK_REG = WB_SCRATCH; n = RS->nq; SVBK_REG = o;
    return n;
}

/* queue the 2x3 hardware positions of a unit standing on cell (x, y) */
static void queue_cell(uint8_t x, uint8_t y)
{
    int16_t wx0 = (int16_t)x * 2, wy0 = (int16_t)y * 2 - 1, wx, wy;
    for (wy = wy0; wy < wy0 + 3; wy++)
        for (wx = wx0; wx < wx0 + 2; wx++) {
            if (wx < tox || wx >= tox + CACHE_W || wy < toy || wy >= toy + CACHE_H) continue;
            if (RS->nq < RS_QMAX) { rs_qx[RS->nq] = (uint8_t)wx; rs_qy[RS->nq] = (uint8_t)(wy & 0xFF); RS->nq++; }
            else q_overflow = 1;               /* too many at once: recompose the whole cache instead */
        }
}

void mapview_prepare(void) BANKED
{
    uint8_t i, o;
    if (edge_kind == 2 && edge_half == 1 && !n_tiles) {          /* second half of a row edge */
        filter_units(tox, tox + CACHE_W - 1, edge_pos, edge_pos);
        for (i = 11; i < CACHE_W; i++) stage_tile(tox + i, edge_pos);
        return;
    }
    if (edge_kind || n_tiles) return;
    {
        uint8_t g = unit_gen;
        if (have_gen && g == diff_gen && !RS_pending()) goto attrs;   /* nothing changed, nothing queued */
        refresh_units();
        diff_gen = g;
    }
    /* diff the drawn unit state; queue old and new cells of every unit that changed (design doc 12.2) */
    o = SVBK_REG;
    SVBK_REG = WB_SCRATCH;
    for (i = 0; i < MAXU; i++) {
        RU *r = &RS->ru[i];
        uint8_t vis = i < n_units_seen ? cur_vis[i] : 0;
        if (r->vis == vis && (!vis || (r->x == snap_x[i] && r->y == snap_y[i] && r->pal == cur_pal[i] && r->body == cur_body[i]))) continue;
        if (r->vis) queue_cell(r->x, r->y);
        if (vis) queue_cell(snap_x[i], snap_y[i]);
        r->vis = vis; r->x = snap_x[i]; r->y = snap_y[i]; r->pal = cur_pal[i]; r->body = cur_body[i];
    }
    if (q_overflow) {                          /* dirty queue overflowed: sweep the whole cache */
        q_overflow = 0;
        RS->nq = 0;
        SVBK_REG = o;
        hb_x0 = tox; hb_x1 = tox + CACHE_W - 1; hb_y0 = toy; hb_y1 = toy + CACHE_H - 1;
        hl_row = 0; hl_half = 0;
        goto attrs;
    }
    /* stage up to 12 dirty positions (2 B queue entries are wx, wy & 0xFF; wy fits since maps <= 64 rows) */
    {
        uint8_t qx[12], qy[12], n = 0;
        while (RS->nq && n < 12u) { RS->nq--; qx[n] = rs_qx[RS->nq]; qy[n] = rs_qy[RS->nq]; n++; }
        SVBK_REG = o;
        for (i = 0; i < n; i++) {
            int16_t wy = (int16_t)(int8_t)qy[i];
            if (qy[i] < 0x80u) wy = qy[i];
            filter_units(qx[i], qx[i], wy, wy);
            stage_tile(qx[i], wy);
        }
        contiguous = 0;
        if (n) return;
    }
attrs:
    while (hl_row < CACHE_H) {                 /* range sweep: one half row (11 positions) per frame */
        int16_t wy = toy + hl_row, x0 = tox + (int16_t)hl_half * 11;
        uint8_t hit = wy >= hb_y0 && wy <= hb_y1 && x0 + 10 >= hb_x0 && x0 <= hb_x1;
        if (++hl_half == 2) { hl_half = 0; hl_row++; }
        if (!hit) continue;                    /* outside both boxes: nothing changes there */
        filter_units(x0, x0 + 10, wy, wy);
        for (i = 0; i < 11; i++) stage_tile(x0 + i, wy);
        contiguous = 0;
        break;
    }
}

static void gdma16(uint8_t bank, uint16_t src, uint16_t dst, uint8_t blocks)
{
    VBK_REG = bank;
    HDMA1_REG = (uint8_t)(src >> 8); HDMA2_REG = (uint8_t)(src & 0xF0u);
    HDMA3_REG = (uint8_t)((dst >> 8) & 0x1Fu); HDMA4_REG = (uint8_t)(dst & 0xF0u);
    HDMA5_REG = (uint8_t)(blocks - 1u);
}

/* VBlank tile loop (design doc 12.1), SM83 assembly: the C version cost ~300-400 M-cycles per tile
 * (a double-speed line is 228), so 16 tiles ran ~6 lines past VBlank and SameBoy/hardware dropped the
 * writes (mode 3). Per tile: check LY (start only on 144..150), then atomically: [GDMA 16 B of
 * STAGE + i*16 -> tile id] + attribute byte + id byte into the BG map. Uses the precomputed TileMeta
 * (WRAM bank7, selected by the caller). Advances n_done; the rest waits for the next VBlank. */
static void commit_loop(void) __naked
{
    __asm
    ld   a, (_n_done)
    ld   b, a
    ld   a, (_n_tiles)
    ld   c, a
1$:
    ld   a, b
    cp   a, c
    jp   nc, 9$
    di                                   ; before the LY read: no ISR between the check and the writes
    ldh  a, (_LY_REG + 0)
    cp   a, #144
    jp   c, 8$
    cp   a, #151                         ; start a tile only on LY 144..150 (>= 3 lines of margin)
    jp   nc, 8$
    ld   hl, #(_mv_tmeta + 16)
    ld   a, l
    add  a, b
    ld   l, a
    adc  a, h
    sub  a, l
    ld   h, a
    ld   e, (hl)
    ld   hl, #_mv_tmeta
    ld   a, l
    add  a, b
    ld   l, a
    adc  a, h
    sub  a, l
    ld   h, a
    ld   d, (hl)
    ld   a, (_contiguous)
    or   a, a
    jr   nz, 2$
    ld   a, e
    rrca
    rrca
    rrca
    and  a, #1
    ldh  (_VBK_REG + 0), a
    ld   a, #0xC1
    ldh  (_HDMA1_REG + 0), a
    ld   a, b
    swap a
    ldh  (_HDMA2_REG + 0), a
    ld   a, d
    swap a
    and  a, #0xF0
    ldh  (_HDMA4_REG + 0), a
    ld   a, d
    bit  7, a
    jr   nz, 3$
    swap a
    and  a, #0x0F
    add  a, #0x10
    jr   4$
3$:
    and  a, #0x7F
    swap a
    and  a, #0x0F
    add  a, #0x08
4$:
    ldh  (_HDMA3_REG + 0), a
    xor  a, a
    ldh  (_HDMA5_REG + 0), a
2$:
    push de
    ld   hl, #(_mv_tmeta + 32)
    ld   a, b
    add  a, a
    add  a, l
    ld   l, a
    adc  a, h
    sub  a, l
    ld   h, a
    ld   a, (hl+)
    ld   e, a
    ld   a, (hl)
    add  a, #0x98
    ld   h, a
    ld   l, e
    pop  de
    ld   a, #1
    ldh  (_VBK_REG + 0), a
    ld   (hl), e
    xor  a, a
    ldh  (_VBK_REG + 0), a
    ld   (hl), d
    ldh  a, (_STAT_REG + 0)              ; test hook: this tile's last write still in VBlank (mode 1)?
    and  a, #3
    cp   a, #1
    jr   z, 5$
    ld   hl, #_dbg_vram_late
    inc  (hl)
5$:
    ei
    inc  b
    jp   1$
8$:
    ei
9$:
    ld   a, b
    ld   (_n_done), a
    ret
    __endasm;
}

void mapview_commit(void) BANKED
{
    uint8_t o;
    /* LY gate (12.1): start only early in VBlank; each tile re-checks, the rest waits for the next VBlank */
    if (LY_REG < 144u || LY_REG > 151u) return;
    if (n_tiles) {
        o = SVBK_REG;
        SVBK_REG = WB_SCRATCH;
        if (contiguous && n_done == 0) {       /* one column: a single GDMA for all tile data (~1.2 lines) */
            uint8_t at0 = TM->at[0], late = 0;
            uint16_t dst = id_addr(TM->id[0]);
            CRITICAL {                         /* atomic LY check + start: no ISR in between (12.1) */
                if (LY_REG > 149u) late = 1;
                else {
                    VBK_REG = (uint8_t)((at0 >> 3) & 1u);
                    HDMA1_REG = 0xC1u; HDMA2_REG = 0x00u;      /* source: STAGE (C100) */
                    HDMA3_REG = (uint8_t)((dst >> 8) & 0x1Fu); HDMA4_REG = (uint8_t)(dst & 0xF0u);
                    HDMA5_REG = (uint8_t)(n_tiles - 1u);
                }
            }
            if (late) { SVBK_REG = o; goto regs; }
        }
        commit_loop();                         /* SM83: ~40 M-cycles per tile, stops when VBlank ends */
        SVBK_REG = o;
        if (n_done == n_tiles) {
            n_tiles = 0; n_done = 0;
            if (edge_kind == 2) { if (edge_half == 0) edge_half = 1; else edge_kind = 0; }
            else edge_kind = 0;
        }
    }
regs:
    VBK_REG = VBK_TILES;
    if (edge_kind == 0) { SCX_REG = (uint8_t)cam_x; SCY_REG = (uint8_t)cam_y; }
}
