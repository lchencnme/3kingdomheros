/* Fullscreen battle presentation (design doc 12.4): layered runtime composition of a background plate
 * and two actors, double-buffered composite slots, GDMA uploads <= 1024 B per VBlank, map flip. */
#ifndef GB_FS_H
#define GB_FS_H
#include <stdint.h>
#include "../core/battle.h"

typedef struct { uint8_t cls, fac; uint16_t name_tx; uint16_t hp; } FsSide;   /* state BEFORE the attack */

/* presents an already-resolved attack (logic is sealed before this is called: design doc 4.4).
 * B skips, holding A halves the pose holds. Returns with the LCD off; the caller restores map mode. */
void fs_present(const FsSide *a, const FsSide *d, const AttackResult *r) BANKED;
#endif
