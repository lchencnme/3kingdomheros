/* Scrolling map view (design doc 10.3 camera, 12.2 edge streaming).
 * TECH01 skeleton: terrain metatiles only; units are temporary OBJ placeholders
 * until the 352-slot BG unit compositor lands. */
#ifndef GB_MAPVIEW_H
#define GB_MAPVIEW_H
#include <stdint.h>

#define VIEW_W 160u
#define VIEW_H 112u

extern uint16_t cam_x, cam_y;

void mapview_init(const uint8_t *meta4, const uint8_t *terrain_pal);   /* call with LCD off */
void mapview_highlight(uint8_t on);    /* uses W7_MR; repaints attributes over a few frames */
void mapview_camera_follow(uint8_t cx, uint8_t cy);  /* dead-zone target, <=2px/frame, one axis */
void mapview_prepare(void);            /* outside VBlank */
void mapview_commit(void);             /* inside VBlank: register/VRAM writes only */
#endif
