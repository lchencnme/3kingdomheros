#include <gb/gb.h>
#include "mapview.h"
#include "wram.h"
#include "../gen/tech01_data.h"

uint16_t cam_x, cam_y;
static int16_t tox, toy;               /* loaded hw-tile origin = floor(cam/8)-1 */
static const uint8_t *meta;            /* [terrain][4] */
static const uint8_t *tpal;
static uint8_t hl_on, hl_row;          /* highlight repaint job: rows 0..15, 16 = idle */

#define BLANK_TILE TECH01_BG_TILES
#define CACHE_W 22
#define CACHE_H 16

/* pending edge */
static uint8_t edge_kind;              /* 0 none, 1 column, 2 row */
static int16_t edge_pos;
static uint8_t e_id[CACHE_W], e_at[CACHE_W];
/* pending attribute row */
static uint8_t ar_pending;
static int16_t ar_y;
static uint8_t ar_at[CACHE_W], e_tmp[CACHE_W];

static uint16_t max_cam_x(void) { uint16_t w = (uint16_t)grid_w * 16u; return w > VIEW_W ? w - VIEW_W : 0; }
static uint16_t max_cam_y(void) { uint16_t h = (uint16_t)grid_h * 16u; return h > VIEW_H ? h - VIEW_H : 0; }

static void hw_tile(int16_t wx, int16_t wy, uint8_t *id, uint8_t *at)
{
    uint8_t cx, cy, t, q, pal;
    if (wx < 0 || wy < 0 || wx >= (int16_t)grid_w * 2 || wy >= (int16_t)grid_h * 2) {
        *id = BLANK_TILE; *at = 0; return;
    }
    cx = (uint8_t)(wx >> 1); cy = (uint8_t)(wy >> 1);
    t = grid_terrain(cx, cy) & 0x0F;
    q = (uint8_t)(((wy & 1) << 1) | (wx & 1));
    *id = meta[t * 4 + q];
    pal = tpal[t];
    if (hl_on) {
        uint8_t o = SVBK_REG;
        SVBK_REG = WB_SCRATCH;
        if (path_dist(W7_MR, cx, cy) != MR_INF) pal |= 4;
        SVBK_REG = o;
    }
    *at = pal;
}

/* build one hardware row: bulk terrain read (one bank switch) + direct highlight lookup */
static void build_row(int16_t wy, int16_t wx0, uint8_t n, uint8_t *ids, uint8_t *ats)
{
    uint8_t ter[CACHE_W / 2 + 2], occ[CACHE_W / 2 + 2], hl[CACHE_W / 2 + 2];
    int16_t cx0 = wx0 >> 1, cy = wy >> 1, c0 = cx0, c1 = (wx0 + n - 1) >> 1;
    uint8_t i, nc, q2;
    int16_t wx;
    if (wy < 0 || cy >= (int16_t)grid_h) {
        for (i = 0; i < n; i++) { ids[i] = BLANK_TILE; ats[i] = 0; }
        return;
    }
    if (c0 < 0) c0 = 0;
    if (c1 >= (int16_t)grid_w) c1 = (int16_t)grid_w - 1;
    nc = c1 >= c0 ? (uint8_t)(c1 - c0 + 1) : 0;
    if (nc) grid_row((uint8_t)cy, (uint8_t)c0, nc, ter, occ);
    for (i = 0; i < nc; i++) hl[i] = 0;
    if (hl_on && nc) {
        uint8_t o = SVBK_REG;
        MoveRange *mr = W7_MR;
        int16_t ly;
        SVBK_REG = WB_SCRATCH;
        ly = cy - mr->by;
        if (ly >= 0 && ly < (int16_t)MR_DIM) {
            for (i = 0; i < nc; i++) {
                int16_t lx = c0 + i - mr->bx;
                if (lx >= 0 && lx < (int16_t)MR_DIM) hl[i] = mr->dist[(uint16_t)ly * MR_DIM + (uint16_t)lx] != MR_INF;
            }
        }
        SVBK_REG = o;
    }
    q2 = (uint8_t)((wy & 1) << 1);
    for (i = 0, wx = wx0; i < n; i++, wx++) {
        int16_t cx = wx >> 1;
        if (wx < 0 || cx < c0 || cx > c1) { ids[i] = BLANK_TILE; ats[i] = 0; continue; }
        {
            uint8_t k = (uint8_t)(cx - c0), tt = ter[k] & 0x0F;
            ids[i] = meta[(uint8_t)(tt << 2) + (q2 | (uint8_t)(wx & 1))];
            ats[i] = (uint8_t)(tpal[tt] | (hl[k] ? 4 : 0));
        }
    }
}

static void write_row_vram(int16_t wy, int16_t wx0, const uint8_t *buf, uint8_t n)
{
    uint8_t *base = (uint8_t *)0x9800 + ((uint16_t)(wy & 31) << 5);
    uint8_t i, x = (uint8_t)(wx0 & 31);
    for (i = 0; i < n; i++) { base[x] = buf[i]; x = (uint8_t)((x + 1) & 31); }
}

static void write_col_vram(int16_t wx, int16_t wy0, const uint8_t *buf, uint8_t n)
{
    uint8_t i, y = (uint8_t)(wy0 & 31), x = (uint8_t)(wx & 31);
    uint8_t *col = (uint8_t *)0x9800 + x;
    for (i = 0; i < n; i++) { col[(uint16_t)y << 5] = buf[i]; y = (uint8_t)((y + 1) & 31); }
}

void mapview_init(const uint8_t *meta4, const uint8_t *terrain_pal)
{
    int16_t x, y;
    uint8_t id, at;
    meta = meta4; tpal = terrain_pal;
    hl_on = 0; hl_row = CACHE_H; edge_kind = 0; ar_pending = 0;
    if (cam_x > max_cam_x()) cam_x = max_cam_x();
    if (cam_y > max_cam_y()) cam_y = max_cam_y();
    tox = (int16_t)(cam_x >> 3) - 1;
    toy = (int16_t)(cam_y >> 3) - 1;
    for (y = toy; y < toy + CACHE_H; y++)
        for (x = tox; x < tox + CACHE_W; x++) {
            hw_tile(x, y, &id, &at);
            VBK_REG = VBK_TILES;
            *((uint8_t *)0x9800 + ((uint16_t)(y & 31) << 5) + (x & 31)) = id;
            VBK_REG = VBK_ATTRIBUTES;
            *((uint8_t *)0x9800 + ((uint16_t)(y & 31) << 5) + (x & 31)) = at;
        }
    VBK_REG = VBK_TILES;
    SCX_REG = (uint8_t)cam_x;
    SCY_REG = (uint8_t)cam_y;
}

void mapview_highlight(uint8_t on)
{
    hl_on = on;
    hl_row = 0;
}

void mapview_camera_follow(uint8_t cx, uint8_t cy)
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
    if (edge_kind) return;                 /* previous edge not committed: hold camera */
    /* one axis per frame, x first; never cross into an unloaded tile */
    if (tx != (int16_t)cam_x) {
        int16_t d = tx - (int16_t)cam_x;
        if (d > 2) d = 2;
        if (d < -2) d = -2;
        ntx = (int16_t)cam_x + d;
        if ((ntx >> 3) - 1 != tox) {
            /* need a new column before showing it */
            int16_t ntox = (ntx >> 3) - 1;
            edge_kind = 1;
            edge_pos = ntox > tox ? ntox + CACHE_W - 1 : ntox;
            {
                uint8_t i;
                for (i = 0; i < CACHE_H; i++) hw_tile(edge_pos, toy + i, &e_id[i], &e_at[i]);
            }
            tox = ntox;
        }
        cam_x = (uint16_t)ntx;
    } else if (ty != (int16_t)cam_y) {
        int16_t d = ty - (int16_t)cam_y;
        if (d > 2) d = 2;
        if (d < -2) d = -2;
        nty = (int16_t)cam_y + d;
        if ((nty >> 3) - 1 != toy) {
            int16_t ntoy = (nty >> 3) - 1;
            edge_kind = 2;
            edge_pos = ntoy > toy ? ntoy + CACHE_H - 1 : ntoy;
            build_row(edge_pos, tox, CACHE_W, e_id, e_at);
            toy = ntoy;
        }
        cam_y = (uint16_t)nty;
    }
}

void mapview_prepare(void)
{
    if (hl_row < CACHE_H && !ar_pending) {
        ar_y = toy + hl_row;
        build_row(ar_y, tox, CACHE_W, e_tmp, ar_at);
        ar_pending = 1;
        hl_row++;
    }
}

void mapview_commit(void)
{
    /* LY gate (design doc 12.1): a late frame commits nothing; edge + SCX stay pending together */
    if (LY_REG < 144u || LY_REG > 150u) return;
    if (edge_kind == 1) {
        VBK_REG = VBK_TILES;      write_col_vram(edge_pos, toy, e_id, CACHE_H);
        VBK_REG = VBK_ATTRIBUTES; write_col_vram(edge_pos, toy, e_at, CACHE_H);
    } else if (edge_kind == 2) {
        VBK_REG = VBK_TILES;      write_row_vram(edge_pos, tox, e_id, CACHE_W);
        VBK_REG = VBK_ATTRIBUTES; write_row_vram(edge_pos, tox, e_at, CACHE_W);
    }
    if (ar_pending) {
        VBK_REG = VBK_ATTRIBUTES; write_row_vram(ar_y, tox, ar_at, CACHE_W);
        ar_pending = 0;
    }
    VBK_REG = VBK_TILES;
    edge_kind = 0;
    SCX_REG = (uint8_t)cam_x;
    SCY_REG = (uint8_t)cam_y;
}
