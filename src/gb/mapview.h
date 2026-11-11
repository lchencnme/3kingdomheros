/* Scrolling map view (design doc 10.3 camera, 12.2 edge streaming).
 * 352-position BG compositor: static units are composited into BG tiles (see mapview.c). */
#ifndef GB_MAPVIEW_H
#define GB_MAPVIEW_H
#include <stdint.h>

#define VIEW_W 160u
#define VIEW_H 112u

extern uint16_t cam_x, cam_y;

#include <gb/gb.h>
void mapview_init(const uint8_t *meta4, const uint8_t *terrain_pal) BANKED;  /* call with LCD off */
void mapview_highlight(uint8_t on) BANKED;   /* uses W7_MR; repaints attributes over a few frames */
void mapview_camera_follow(uint8_t cx, uint8_t cy) BANKED;  /* dead-zone target, <=2px/frame, one axis */
void mapview_prepare(void) BANKED;     /* outside VBlank: unit diff, dirty/edge tile composition */
void mapview_commit(void) BANKED;      /* inside VBlank: GDMA + map/attr writes only */
void mapview_set_units(uint8_t n) BANKED;
void mapview_treasures(void) BANKED;
void mapview_danger(uint8_t on) BANKED;  /* show the danger bitmap at WRAM7 DC00 (X marks), sweep redraw */   /* re-read the untaken treasures (7.4); redraws changed cells */
#endif
