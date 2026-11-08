/* Dialogue mode (design doc 12.3): 64x64 portrait at (8,8), name line + 2 text lines below. */
#ifndef GB_DIALOG_H
#define GB_DIALOG_H
#include <stdint.h>
/* plays every queued page (A = next page), clears the queue; returns with the LCD off */
void dialog_run(void) BANKED;
#endif
