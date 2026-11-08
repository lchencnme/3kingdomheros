/* Entry point: must live in fixed bank 0; the game itself runs from code bank 5. */
#include <gb/gb.h>

void game_run(void) BANKED;

void main(void)
{
    game_run();
}
