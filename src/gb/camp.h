/* camp node screen (design doc 2.2 / 2.3) */
#ifndef GB_CAMP_H
#define GB_CAMP_H
#include <stdint.h>
#include <gb/gb.h>

#define CAMP_DBG_STATE 20u        /* dbg_state = 20 + screen while the camp is open (tests) */
#define CAMP_MAX_DEPLOY 4u

/* deployment result: officer indices (0 刘备, 1 关羽, 2 张飞, 3 简雍) in slot order */
typedef struct { uint8_t n; uint8_t order[CAMP_MAX_DEPLOY]; } CampDeploy;

/* blocking: returns with the LCD off when the player departs */
void camp_run(uint16_t *gold, CampDeploy *dep) BANKED;
#endif
