#include <gb/gb.h>
#include "input.h"

uint8_t in_cur, in_pressed;
int8_t in_dx, in_dy;
static uint8_t hold, last_axis;   /* last_axis: 0 vertical, 1 horizontal */
static volatile uint8_t isr_prev, isr_latch;

/* VBlank ISR: latch rising edges so a short press is never lost if a main-loop frame overruns */
static void input_vbl(void) NONBANKED   /* ISR: must live in fixed bank 0 (design doc 11.3) */
{
    uint8_t raw = joypad();
    isr_latch |= (uint8_t)(raw & ~isr_prev);
    isr_prev = raw;
}

void input_init(void)
{
    CRITICAL { add_VBL(input_vbl); }
}

#define REPEAT_DELAY 12u
#define REPEAT_RATE 4u

void input_update(void)
{
    uint8_t dirs, newd;
    int8_t vx = 0, vy = 0;
    uint8_t latch, raw;
    /* held state and rising edges both come from the same VBlank sample (no double counting) */
    CRITICAL { latch = isr_latch; isr_latch = 0; raw = isr_prev; }
    in_cur = (uint8_t)(raw | latch);           /* a latched tap counts as held for this frame */
    in_pressed = latch;                        /* rising edges come only from the VBlank sampler */
    dirs = (uint8_t)(in_cur & (J_UP | J_DOWN | J_LEFT | J_RIGHT));
    newd = (uint8_t)(in_pressed & (J_UP | J_DOWN | J_LEFT | J_RIGHT));

    if ((dirs & J_UP) && !(dirs & J_DOWN)) vy = -1;
    if ((dirs & J_DOWN) && !(dirs & J_UP)) vy = 1;
    if ((dirs & J_LEFT) && !(dirs & J_RIGHT)) vx = -1;
    if ((dirs & J_RIGHT) && !(dirs & J_LEFT)) vx = 1;

    /* most recently pressed axis wins; exactly simultaneous: vertical first */
    if (newd & (J_UP | J_DOWN)) last_axis = 0;
    else if (newd & (J_LEFT | J_RIGHT)) last_axis = 1;
    if (vx && vy) { if (last_axis == 0) vx = 0; else vy = 0; }

    in_dx = 0; in_dy = 0;
    if (!vx && !vy) { hold = 0; return; }
    if (newd) hold = 0;
    if (hold == 0 || (hold >= REPEAT_DELAY && ((hold - REPEAT_DELAY) % REPEAT_RATE) == 0)) {
        in_dx = vx; in_dy = vy;
    }
    if (hold < 250) hold++;
}
