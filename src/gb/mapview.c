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
#include "../gen/tech01_data.h"

uint16_t cam_x, cam_y;
static int16_t tox, toy;               /* loaded hw-tile origin = floor(cam/8)-1 */
static const uint8_t *meta;            /* [terrain][4] */
static const uint8_t *tpal;
static uint8_t hl_on, hl_row;          /* highlight attribute repaint job: rows 0..15, 16 = idle */

#define BLANK_ID_SLOT 0xFFFFu
#define CACHE_W 22
#define CACHE_H 16
#define PAL_PLAYER 4u
#define PAL_ENEMY 5u
#define PAL_HL 6u
#define PAL_ACTED 7u
#define MAXU 12u

/* render state + dirty queue in WRAM bank7 D400..D5FF (the former full-map inqueue area, now unused) */
typedef struct { uint8_t x, y, vis, pal, body; } RU;
typedef struct {
    RU ru[MAXU];                       /* what is currently drawn */
    uint8_t nq;
    uint8_t qx[64], qy[64];            /* dirty world hardware positions */
} RenderState;
#define RS ((RenderState *)0xD400u)
typedef char rs_fits[(sizeof(RenderState) <= 0x200u) ? 1 : -1];

extern uint8_t snap_x[MAXU], snap_y[MAXU], snap_fac[MAXU], snap_flags[MAXU], snap_cls[MAXU];
extern uint16_t snap_hp[MAXU];
void units_snapshot(uint8_t n);
static uint8_t n_units_seen;
extern uint8_t unit_gen;
static uint8_t seen_gen, have_gen, diff_gen;
static uint8_t order[MAXU], n_order;   /* visible units sorted by (foot y, id) */
static uint8_t cand[MAXU], n_cand;     /* subset of order that can touch the current edge / position */

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
static uint8_t cur_vis[MAXU], cur_pal[MAXU], cur_body[MAXU];

/* staging: C100 (256 B, design doc 11.4); pending transfers built in the visible period */
#define STAGE ((uint8_t *)0xC100u)
static uint8_t edge_kind;              /* 0 none, 1 column, 2 row (two halves) */
static int16_t edge_pos;
static uint8_t edge_half;
static uint8_t n_tiles;                /* staged tiles (16 B each) */
static uint8_t t_wx1[16], t_wy1[16];   /* world hw position + 1 (so -1 fits in a byte) */
static uint8_t t_at[16];
#define T_WX(i) ((int16_t)t_wx1[i] - 1)
#define T_WY(i) ((int16_t)t_wy1[i] - 1)
#define T_SLOT(i) slot_of(T_WX(i), T_WY(i))
static uint8_t contiguous;             /* staged tiles form one contiguous slot run */
/* pending attribute row (highlight job) */
static uint8_t ar_pending;
static int16_t ar_y;
static uint8_t ar_at[CACHE_W];

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
        cur_body[i] = body_of(snap_cls[i]);
        if (!vis) continue;
        for (k = n_order; k > 0 && (snap_y[order[k - 1]] > snap_y[i]); k--) order[k] = order[k - 1];
        order[k] = i;
        n_order++;
    }
}

/* compose world hardware tile (wx, wy) into dst (16 B); returns its attribute (palette | bank bit) */
static uint8_t compose(int16_t wx, int16_t wy, uint8_t *dst, uint8_t hl)
{
    uint8_t pal = 0, k, under = 0, y;
    if (wx < 0 || wy < 0 || wx >= (int16_t)grid_w * 2 || wy >= (int16_t)grid_h * 2) {
        memset(dst, 0, 16);
    } else {
        uint8_t cx = (uint8_t)(wx >> 1), cy = (uint8_t)(wy >> 1), t = grid_terrain(cx, cy) & 0x0F;
        uint8_t q = (uint8_t)(((wy & 1) << 1) | (wx & 1));
        memcpy(dst, &tech01_bg_tiles[(uint16_t)meta[t * 4 + q] * 16u], 16);   /* same ROM bank as this code */
        pal = hl ? PAL_HL : tpal[t];
    }
    /* units whose 16x24 box covers this position, in draw order (pre-filtered candidates) */
    for (k = 0; k < n_cand; k++) {
        uint8_t u = cand[k];
        int16_t lx = wx - (int16_t)snap_x[u] * 2, ly = wy - ((int16_t)snap_y[u] * 2 - 1);
        const uint8_t *ut;
        if (lx < 0 || lx > 1 || ly < 0 || ly > 2) continue;
        ut = &tech01_unit_tiles[((uint16_t)cur_body[u] * 6u + (uint16_t)ly * 2u + (uint16_t)lx) * UNIT_TILE_BYTES];
        if (!under) {      /* terrain under a unit keeps only its ink (colour 3) */
            for (y = 0; y < 8; y++) { uint8_t ink = (uint8_t)(dst[2 * y] & dst[2 * y + 1]); dst[2 * y] = ink; dst[2 * y + 1] = ink; }
            under = 1;
        }
        for (y = 0; y < 8; y++) {
            uint8_t m = ut[16 + y], nm = (uint8_t)~m;
            dst[2 * y] = (uint8_t)((dst[2 * y] & nm) | ut[2 * y]);
            dst[2 * y + 1] = (uint8_t)((dst[2 * y + 1] & nm) | ut[2 * y + 1]);
        }
        pal = cur_pal[u];
    }
    return pal;
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

static uint8_t attr_of(int16_t wx, int16_t wy)
{
    uint8_t k, pal = 0;
    for (k = 0; k < n_order; k++) {
        uint8_t u = order[k];
        int16_t lx = wx - (int16_t)snap_x[u] * 2, ly = wy - ((int16_t)snap_y[u] * 2 - 1);
        if (lx >= 0 && lx <= 1 && ly >= 0 && ly <= 2) pal = cur_pal[u];
    }
    if (!pal) {
        if (wx < 0 || wy < 0 || wx >= (int16_t)grid_w * 2 || wy >= (int16_t)grid_h * 2) pal = 0;
        else pal = in_range_hl(wx, wy) ? PAL_HL : tpal[grid_terrain((uint8_t)(wx >> 1), (uint8_t)(wy >> 1)) & 0x0F];
    }
    return (uint8_t)(pal | (slot_bank(slot_of(wx, wy)) ? 0x08u : 0u));
}

static void stage_tile(int16_t wx, int16_t wy)
{
    uint16_t s = slot_of(wx, wy);
    uint8_t at = compose(wx, wy, STAGE + (uint16_t)n_tiles * 16u, in_range_hl(wx, wy));
    t_wx1[n_tiles] = (uint8_t)(wx + 1); t_wy1[n_tiles] = (uint8_t)(wy + 1);
    t_at[n_tiles] = (uint8_t)(at | (slot_bank(s) ? 0x08u : 0u));
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
    hl_on = 0; hl_row = CACHE_H; edge_kind = 0; ar_pending = 0; n_tiles = 0;
    n_units_seen = 12;
    have_gen = 0;
    if (cam_x > max_cam_x()) cam_x = max_cam_x();
    if (cam_y > max_cam_y()) cam_y = max_cam_y();
    tox = (int16_t)(cam_x >> 3) - 1;
    toy = (int16_t)(cam_y >> 3) - 1;
    refresh_units();
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
            uint8_t at = compose(x, y, buf, 0);
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

void mapview_highlight(uint8_t on) BANKED
{
    hl_on = on;
    hl_row = 0;
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
            if (RS->nq < 64u) { RS->qx[RS->nq] = (uint8_t)wx; RS->qy[RS->nq] = (uint8_t)(wy & 0xFF); RS->nq++; }
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
    /* stage up to 12 dirty positions (2 B queue entries are wx, wy & 0xFF; wy fits since maps <= 64 rows) */
    {
        uint8_t qx[12], qy[12], n = 0;
        while (RS->nq && n < 12u) { RS->nq--; qx[n] = RS->qx[RS->nq]; qy[n] = RS->qy[RS->nq]; n++; }
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
    if (hl_row < CACHE_H && !ar_pending) {
        ar_y = toy + hl_row;
        for (i = 0; i < CACHE_W; i++) ar_at[i] = attr_of(tox + i, ar_y);
        ar_pending = 1;
        hl_row++;
    }
}

static void gdma16(uint8_t bank, uint16_t src, uint16_t dst, uint8_t blocks)
{
    VBK_REG = bank;
    HDMA1_REG = (uint8_t)(src >> 8); HDMA2_REG = (uint8_t)(src & 0xF0u);
    HDMA3_REG = (uint8_t)((dst >> 8) & 0x1Fu); HDMA4_REG = (uint8_t)(dst & 0xF0u);
    HDMA5_REG = (uint8_t)(blocks - 1u);
}

void mapview_commit(void) BANKED
{
    uint8_t i;
    /* LY gate (design doc 12.1): a late frame commits nothing; staged tiles + SCX stay pending together */
    if (LY_REG < 144u || LY_REG > 149u) return;
    if (n_tiles) {
        if (contiguous) { uint16_t s0 = T_SLOT(0); gdma16(slot_bank(s0), (uint16_t)STAGE, id_addr(slot_id(s0)), n_tiles); }
        else for (i = 0; i < n_tiles; i++) { uint16_t s = T_SLOT(i); gdma16(slot_bank(s), (uint16_t)STAGE + (uint16_t)i * 16u, id_addr(slot_id(s)), 1); }
        for (i = 0; i < n_tiles; i++) {
            uint16_t s = T_SLOT(i);
            VBK_REG = VBK_TILES; write_map_byte(T_WX(i), T_WY(i), slot_id(s));
            VBK_REG = VBK_ATTRIBUTES; write_map_byte(T_WX(i), T_WY(i), t_at[i]);
        }
        n_tiles = 0;
        if (edge_kind == 2) { if (edge_half == 0) edge_half = 1; else edge_kind = 0; }
        else edge_kind = 0;
    }
    if (ar_pending) {
        uint8_t *base = (uint8_t *)0x9800u + ((uint16_t)(ar_y & 31) << 5);
        uint8_t x = (uint8_t)(tox & 31);
        VBK_REG = VBK_ATTRIBUTES;
        for (i = 0; i < CACHE_W; i++) { base[x] = ar_at[i]; x = (uint8_t)((x + 1) & 31); }
        ar_pending = 0;
    }
    VBK_REG = VBK_TILES;
    if (edge_kind == 0) { SCX_REG = (uint8_t)cam_x; SCY_REG = (uint8_t)cam_y; }
}
