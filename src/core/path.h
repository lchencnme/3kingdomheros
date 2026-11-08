/* Movement range, path backtrack, reverse approach field, line of sight (design doc 4.2, 4.4). */
#ifndef CORE_PATH_H
#define CORE_PATH_H
#include <stdint.h>
#include "grid.h"

#define MR_DIM 19u
#define MR_CELLS 361u
#define MR_INF 255u
#define FIELD_INF 0xFFFFu
#define FIELD_MAX_CELLS 4096u

/* ~1497 bytes; lives in WRAM bank7 0xD600..0xDBFF on GB */
typedef struct {
    int8_t bx, by;              /* box origin (may be negative) */
    uint8_t sx, sy, move, mtype, side;
    uint8_t dist[MR_CELLS];     /* MR_INF = unreachable */
    uint8_t cost[MR_CELLS];     /* entry cost snapshot of the box; 255 = impassable/hostile/outside */
    uint8_t inq[(MR_CELLS + 7u) / 8u];
    uint8_t qx[MR_CELLS], qy[MR_CELLS];   /* ring queue of local coords (no division in the loop) */
} MoveRange;

/* compute bounded SPFA movement range for a mover of 'side' (SIDE_A/SIDE_E) */
void path_move_range(MoveRange *mr, uint8_t x, uint8_t y, uint8_t move, uint8_t mtype, uint8_t side);
/* distance to (x,y) or MR_INF */
uint8_t path_dist(const MoveRange *mr, uint8_t x, uint8_t y);
/* legal endpoint: reachable and empty (or the start cell) */
uint8_t path_endpoint_ok(const MoveRange *mr, uint8_t x, uint8_t y);
/* write path start..end as global cell indices (y*w+x); returns length or 0 if unreachable */
uint8_t path_backtrack(const MoveRange *mr, uint8_t ex, uint8_t ey, uint16_t *out, uint8_t max);

typedef struct {
    uint8_t mtype, side, done;
    uint16_t head, count;
} FieldJob;

/* targets: ascending unique cell indices (multi-source) */
void field_begin(FieldJob *j, uint8_t mtype, uint8_t side, const uint16_t *targets, uint8_t ntargets);
/* process up to 'budget' pops; returns 1 when complete */
uint8_t field_step(FieldJob *j, uint16_t budget);

/* 1 if no wall strictly between the two cell centres (supercover) */
uint8_t los_clear(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);

uint8_t path_cost(uint8_t x, uint8_t y, uint8_t mtype);
#endif
