/* Input sampling with the repeat rules of design doc 10.2 */
#ifndef GB_INPUT_H
#define GB_INPUT_H
#include <stdint.h>

extern uint8_t in_cur, in_pressed;   /* raw state, rising edges (A/B/START/SELECT) */
extern int8_t in_dx, in_dy;          /* one-step cursor move this frame (0/±1, one axis only) */

void input_init(void);
void input_update(void);
#endif
