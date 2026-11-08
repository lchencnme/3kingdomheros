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

void field_begin(FieldJob *j, uint8_t mtype, uint8_t side, const uint16_t *targets, uint8_t ntargets) CORE_API
{
    uint16_t i, cells = (uint16_t)grid_w * grid_h;
    j->mtype = mtype; j->side = side; j->done = 0;
    j->head = 0; j->count = 0;
    for (i = 0; i < cells; i++) { fld_set(i, FIELD_INF); fin_clear(i); }
    for (i = 0; i < ntargets; i++) {
        uint16_t t = targets[i];
        if (fld_get(t) == 0) continue;
        fld_set(t, 0);
        fq_set((j->head + j->count) % FIELD_MAX_CELLS, t);
        j->count++;
        fin_set(t);
    }
    if (!j->count) j->done = 1;
}

uint8_t field_step(FieldJob *j, uint16_t budget) CORE_API
{
    while (j->count && budget) {
        uint16_t u = fq_get(j->head);
        uint8_t ux = (uint8_t)(u % grid_w), uy = (uint8_t)(u / grid_w), k;
        uint8_t cu = cost_of(ux, uy, j->mtype);
        uint16_t du = fld_get(u);
        j->head = (uint16_t)((j->head + 1) % FIELD_MAX_CELLS);
        j->count--;
        budget--;
        fin_clear(u);
        if (cu == 255) continue;               /* cannot enter u: no predecessor uses it */
        for (k = 0; k < 4; k++) {
            int16_t vx = (int16_t)ux + NDX[k], vy = (int16_t)uy + NDY[k];
            uint16_t v, nd;
            if (vx < 0 || vy < 0 || vx >= grid_w || vy >= grid_h) continue;
            if (cost_of((uint8_t)vx, (uint8_t)vy, j->mtype) == 255) continue;   /* mover cannot stand on v */
            if (hostile(grid_occ((uint8_t)vx, (uint8_t)vy), j->side)) continue;
            v = (uint16_t)vy * grid_w + (uint16_t)vx;
            nd = du + cu;
            if (nd < fld_get(v)) {
                fld_set(v, nd);
                if (!fin_test(v)) {
                    fq_set((j->head + j->count) % FIELD_MAX_CELLS, v);
                    j->count++;
                    fin_set(v);
                }
            }
        }
    }
    if (!j->count) j->done = 1;
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
