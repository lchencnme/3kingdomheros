#ifdef __SDCC
#pragma bank 3
#endif
#include "path.h"
#include <string.h>
#include "../gen/rules_data.h"

static const uint8_t BIT[8] = {1, 2, 4, 8, 16, 32, 64, 128};

/* neighbour order N, W, E, S */
static const int8_t NDX[4] = {0, -1, 1, 0};
static const int8_t NDY[4] = {-1, 0, 0, 1};

static uint8_t cost_of(uint8_t x, uint8_t y, uint8_t mtype)
{
    return terrain_defs[grid_terrain(x, y) & 0x0F].cost[mtype];
}

uint8_t path_cost(uint8_t x, uint8_t y, uint8_t mtype) CORE_API
{
    return cost_of(x, y, mtype);
}

static const uint16_t mr_row[MR_DIM] = {
    0, 19, 38, 57, 76, 95, 114, 133, 152, 171, 190, 209, 228, 247, 266, 285, 304, 323, 342};

static uint8_t hostile(uint8_t occ, uint8_t side)
{
    return occ != 0 && occ != side;
}

void path_move_range(MoveRange *mr, uint8_t x, uint8_t y, uint8_t move, uint8_t mtype, uint8_t side) CORE_API
{
    uint16_t i, head = 0, tail = 0, count = 0;
    uint8_t lx, ly, lut[16];
    uint8_t ter[MR_DIM], occ[MR_DIM];
    if (move > 9) move = 9;
    mr->bx = (int8_t)((int16_t)x - move);
    mr->by = (int8_t)((int16_t)y - move);
    mr->sx = x; mr->sy = y; mr->move = move; mr->mtype = mtype; mr->side = side;
    memset(mr->inq, 0, sizeof(mr->inq));
    memset(mr->dist, MR_INF, MR_CELLS);
    memset(mr->cost, 255, MR_CELLS);
    for (i = 0; i < 16; i++) lut[i] = i < NUM_TERRAIN ? terrain_defs[i].cost[mtype] : 255;

    /* snapshot entry costs for the active (2*move+1)^2 box: one bulk row read per row */
    for (ly = 0; ly <= (uint8_t)(2 * move); ly++) {
        int16_t gy = mr->by + ly;
        int16_t gx0 = mr->bx, gx1 = mr->bx + 2 * (int16_t)move + 1;   /* [gx0, gx1) */
        uint8_t first = 0, n, k;
        uint8_t *cst;
        if (gy < 0 || gy >= grid_h) continue;
        if (gx0 < 0) { first = (uint8_t)(-gx0); gx0 = 0; }
        if (gx1 > grid_w) gx1 = grid_w;
        if (gx1 <= gx0) continue;
        n = (uint8_t)(gx1 - gx0);
        grid_row((uint8_t)gy, (uint8_t)gx0, n, ter, occ);
        cst = &mr->cost[mr_row[ly] + first];
        for (k = 0; k < n; k++) {
            uint8_t c = lut[ter[k] & 0x0F];
            if (c > move || hostile(occ[k], side)) c = 255;
            cst[k] = c;
        }
    }
    (void)lx;

    i = (uint16_t)move * MR_DIM + move;          /* start is box centre */
    mr->dist[i] = 0;
    mr->qx[0] = move; mr->qy[0] = move; tail = 1; count = 1;
    mr->inq[i >> 3] |= BIT[i & 7];

    while (count) {
        uint8_t ux = mr->qx[head], uy = mr->qy[head], k;
        uint16_t u = mr_row[uy] + ux;
        uint8_t du = mr->dist[u];
        if (++head == MR_CELLS) head = 0;
        count--;
        mr->inq[u >> 3] &= (uint8_t)~BIT[u & 7];
        for (k = 0; k < 4; k++) {
            uint8_t vx = ux, vy = uy, c, nd;
            uint16_t v;
            switch (k) {                     /* N, W, E, S */
            case 0: if (!uy) continue; vy--; v = u - MR_DIM; break;
            case 1: if (!ux) continue; vx--; v = u - 1; break;
            case 2: if (ux == MR_DIM - 1) continue; vx++; v = u + 1; break;
            default: if (uy == MR_DIM - 1) continue; vy++; v = u + MR_DIM; break;
            }
            c = mr->cost[v];
            if (c == 255) continue;
            nd = (uint8_t)(du + c);
            if (nd > move || nd >= mr->dist[v]) continue;
            mr->dist[v] = nd;
            if (!(mr->inq[v >> 3] & BIT[v & 7])) {
                mr->qx[tail] = vx; mr->qy[tail] = vy;
                if (++tail == MR_CELLS) tail = 0;
                count++;
                mr->inq[v >> 3] |= BIT[v & 7];
            }
        }
    }
}

static int16_t local_index(const MoveRange *mr, uint8_t x, uint8_t y)
{
    int16_t lx = (int16_t)x - mr->bx, ly = (int16_t)y - mr->by;
    if (lx < 0 || ly < 0 || lx >= (int16_t)MR_DIM || ly >= (int16_t)MR_DIM) return -1;
    return (int16_t)(mr_row[ly] + lx);
}

uint8_t path_dist(const MoveRange *mr, uint8_t x, uint8_t y) CORE_API
{
    int16_t li;
    if (x >= grid_w || y >= grid_h) return MR_INF;
    li = local_index(mr, x, y);
    return li < 0 ? MR_INF : mr->dist[li];
}

uint8_t path_endpoint_ok(const MoveRange *mr, uint8_t x, uint8_t y) CORE_API
{
    if (path_dist(mr, x, y) == MR_INF) return 0;
    if (x == mr->sx && y == mr->sy) return 1;
    return grid_occ(x, y) == 0;
}

uint8_t path_backtrack(const MoveRange *mr, uint8_t ex, uint8_t ey, uint16_t *out, uint8_t max) CORE_API
{
    uint8_t cx = ex, cy = ey, n = 0, i;
    uint8_t len;
    uint8_t d = path_dist(mr, ex, ey);
    if (d == MR_INF) return 0;
    /* walk backwards; worst case path length is move+1 <= 10 */
    {
        uint16_t tmp[MR_DIM];
        tmp[n++] = (uint16_t)cy * grid_w + cx;
        while (!(cx == mr->sx && cy == mr->sy)) {
            uint8_t cc = cost_of(cx, cy, mr->mtype);
            uint8_t cd = path_dist(mr, cx, cy);
            uint16_t best = 0xFFFF;
            uint8_t bx = 0, by = 0, k;
            for (k = 0; k < 4; k++) {
                int16_t px = (int16_t)cx + NDX[k], py = (int16_t)cy + NDY[k];
                uint8_t pd;
                uint16_t gi;
                if (px < 0 || py < 0 || px >= grid_w || py >= grid_h) continue;
                pd = path_dist(mr, (uint8_t)px, (uint8_t)py);
                if (pd == MR_INF || (uint16_t)pd + cc != cd) continue;
                gi = (uint16_t)py * grid_w + (uint16_t)px;
                if (gi < best) { best = gi; bx = (uint8_t)px; by = (uint8_t)py; }
            }
            if (best == 0xFFFF || n >= MR_DIM) return 0;   /* inconsistent field */
            cx = bx; cy = by;
            tmp[n++] = best;
        }
        if (n > max) return 0;
        len = n;
        for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    }
    return len;
}

/* ---- reverse approach field: dist[v] = min(dist[v], dist[u] + cost[u]) ---- */

static uint8_t f_ter[64], f_occ[64];   /* row snapshot (fixed RAM: grid_row needs a non-banked buffer) */

/* NOTE (GB): FieldJob may live in a banked WRAM page (AI scratch, bank 7). It is only read/written
 * while SVBK is the caller's bank; all work in between uses locals (fc_ptr/fq_ptr switch SVBK). */
void field_begin(FieldJob *j, uint8_t mtype, uint8_t side, const uint16_t *targets, uint8_t ntargets) CORE_API
{
    uint8_t lut[16], x, y, k;
    uint16_t idx = 0, count = 0;
    SVBK_SAVE(o);
    for (k = 0; k < 16; k++) lut[k] = k < NUM_TERRAIN ? terrain_defs[k].cost[mtype] : 255;
    for (y = 0; y < grid_h; y++) {
        grid_row(y, 0, grid_w, f_ter, f_occ);
        for (x = 0; x < grid_w; x++, idx++) {
            uint8_t c = lut[f_ter[x] & 0x0F];
            uint8_t *p;
            if (c == 255 || hostile(f_occ[x], side)) c = FC_BLOCK;
            p = fc_ptr(idx);
            p[0] = FC_UNREACHED; p[1] = c;
        }
        SVBK_RESTORE(o);
    }
    for (k = 0; k < ntargets; k++) {
        uint16_t tc;
        uint8_t tx, ty, c, *p;
        SVBK_RESTORE(o);
        tc = targets[k];                               /* targets may live in the caller's bank */
        ty = (uint8_t)(tc / grid_w); tx = (uint8_t)(tc - (uint16_t)ty * grid_w);
        c = lut[grid_terrain(tx, ty) & 0x0F];          /* targets ignore their own occupancy */
        p = fc_ptr(tc);
        if (p[0] == 0) continue;
        p[0] = 0;
        p[1] = (uint8_t)((c == 255 ? FC_BLOCK : c) | FC_INQ);
        p = fq_ptr(count);
        p[0] = tx; p[1] = ty;
        count++;
    }
    SVBK_RESTORE(o);
    j->mtype = mtype; j->side = side;
    j->head = 0; j->count = count;
    j->done = count ? 0 : 1;
}

#define F_RELAX(VX, VY, V) do {                                              \
        uint8_t *pv = fc_ptr(V), cv = pv[1];                                    \
        if ((cv & 0x7Fu) != FC_BLOCK && nd < pv[0]) {                           \
            pv[0] = nd;                                                         \
            if (!(cv & FC_INQ)) {                                               \
                uint8_t *qq;                                                    \
                pv[1] = (uint8_t)(cv | FC_INQ);                                 \
                qq = fq_ptr((uint16_t)((head + count) & (FIELD_MAX_CELLS - 1u))); \
                qq[0] = (VX); qq[1] = (VY);                                     \
                count++;                                                        \
            }                                                                   \
        }                                                                       \
    } while (0)

uint8_t field_step(FieldJob *j, uint16_t budget) CORE_API
{
    uint8_t w = grid_w, h = grid_h;
    uint16_t head = j->head, count = j->count;
    SVBK_SAVE(o);
    while (count && budget) {
        uint8_t *q = fq_ptr(head), ux = q[0], uy = q[1], cu, nd, *p;
        uint16_t u = (uint16_t)uy * w + ux;
        head = (uint16_t)((head + 1u) & (FIELD_MAX_CELLS - 1u));
        count--;
        budget--;
        p = fc_ptr(u);
        cu = (uint8_t)(p[1] & (uint8_t)~FC_INQ);
        p[1] = cu;
        if (cu == FC_BLOCK) continue;                  /* cannot enter u: no predecessor uses it */
        nd = (uint8_t)(p[0] + cu);
        if (nd > FC_SATURATE || nd < p[0]) nd = FC_SATURATE;
        if (uy) F_RELAX(ux, (uint8_t)(uy - 1u), u - w);          /* N */
        if (ux) F_RELAX((uint8_t)(ux - 1u), uy, u - 1u);         /* W */
        if (ux + 1u < w) F_RELAX((uint8_t)(ux + 1u), uy, u + 1u); /* E */
        if (uy + 1u < h) F_RELAX(ux, (uint8_t)(uy + 1u), u + w); /* S */
    }
    SVBK_RESTORE(o);                                  /* back to the caller's bank before touching j */
    j->head = head; j->count = count;
    if (!count) j->done = 1;
    return j->done;
}

/* ---- supercover line of sight (excludes endpoints) ---- */

static uint8_t is_wall(int16_t x, int16_t y)
{
    return terrain_defs[grid_terrain((uint8_t)x, (uint8_t)y) & 0x0F].blocks_los;
}

uint8_t los_clear(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) CORE_API
{
    int16_t x = x0, y = y0, dx = (int16_t)x1 - x0, dy = (int16_t)y1 - y0;
    int16_t xstep = 1, ystep = 1, ddx, ddy, err, errprev, i;
    if (dx < 0) { xstep = -1; dx = -dx; }
    if (dy < 0) { ystep = -1; dy = -dy; }
    if (dx + dy <= 1) return 1;
    ddx = 2 * dx; ddy = 2 * dy;
#define CHK(px, py) do { if (!((px) == x1 && (py) == y1) && !((px) == x0 && (py) == y0) && is_wall(px, py)) return 0; } while (0)
    if (ddx >= ddy) {
        errprev = err = dx;
        for (i = 0; i < dx; i++) {
            x += xstep; err += ddy;
            if (err > ddx) {
                y += ystep; err -= ddx;
                if (err + errprev < ddx) CHK(x, y - ystep);
                else if (err + errprev > ddx) CHK(x - xstep, y);
                else { CHK(x, y - ystep); CHK(x - xstep, y); }
            }
            CHK(x, y);
            errprev = err;
        }
    } else {
        errprev = err = dy;
        for (i = 0; i < dy; i++) {
            y += ystep; err += ddx;
            if (err > ddy) {
                x += xstep; err -= ddy;
                if (err + errprev < ddy) CHK(x - xstep, y);
                else if (err + errprev > ddy) CHK(x, y - ystep);
                else { CHK(x - xstep, y); CHK(x, y - ystep); }
            }
            CHK(x, y);
            errprev = err;
        }
    }
#undef CHK
    return 1;
}
