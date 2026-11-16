#pragma bank 1   /* the battle result page; bank 10 (camp, dialogue, VBlank) is full */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "dialog.h"
#include "hud.h"
#include "rom.h"
#include "input.h"
#include "audio.h"
#include "debug.h"
#include "game.h"
#include "../gen/scroll_data.h"
#include "../gen/text_data.h"
#include "../gen/fs_data.h"

/* same VRAM layout as the dialogue page (dialog.c): bank1 ids 0..107 the three text lines, bank0 68..167
 * the landscape, 168 the 回纹 divider; palettes 1 landscape, 7 paper/ink. The LCD is off while it is built. */
#define TEXT_PAL 7u
#define NAME_ID 0u
#define LINE1_ID 36u
#define LINE2_ID 72u
static const uint8_t zero16[16];
extern uint8_t cur_bt;

static void put_text_map(void)
{
    uint8_t r, c, ids[20], at[20];
    for (r = 11; r < 17; r++) {
        uint8_t base = r < 13 ? NAME_ID : (r < 15 ? LINE1_ID : LINE2_ID);
        for (c = 0; c < 20; c++) {
            if (c == 0 || c == 19) { ids[c] = 0; at[c] = TEXT_PAL; continue; }
            ids[c] = (uint8_t)(base + ((r - 11u) & 1u) * 18u + (c - 1u));
            at[c] = (uint8_t)(0x08u | TEXT_PAL);
        }
        VBK_REG = 1; set_bkg_tiles(0, r, 20, 1, at);
        VBK_REG = 0; set_bkg_tiles(0, r, 20, 1, ids);
    }
}

static void scroll_panel(void)
{
    uint8_t ids[20], at[20], k;
    uint16_t pal[4];
    const uint8_t *src;
    rom_copy(SCROLL_BANK, pal, scroll_pal, sizeof pal);
    set_bkg_palette(1, 1, pal);
    VBK_REG = 0;
    rom_copy(SCROLL_BANK, &k, &scroll_of_battle[cur_bt], 1);
    rom_copy(SCROLL_BANK, &src, &scroll_ptr[k], sizeof src);
    rom_set_bkg_data(SCROLL_BANK, 68, SCROLL_TILES, src);
    rom_set_bkg_data(SCROLL_BANK, 168, 1, scroll_fret);
    memset(at, 1, 20);
    memset(ids, 168, 20);
    VBK_REG = 1; set_bkg_tiles(0, 10, 20, 1, at);
    VBK_REG = 0; set_bkg_tiles(0, 10, 20, 1, ids);
}

static void scroll_at(uint8_t col0)
{
    uint8_t r, c, ids[20], at[20];
    for (r = 0; r < 10; r++) {
        for (c = 0; c < 20; c++) {
            if (c >= col0 && c < col0 + 10u) { ids[c] = (uint8_t)(68u + r * 10u + (c - col0)); at[c] = 1; }
            else { ids[c] = 0; at[c] = TEXT_PAL; }
        }
        VBK_REG = 1; set_bkg_tiles(0, r, 20, 1, at);
        VBK_REG = 0; set_bkg_tiles(0, r, 20, 1, ids);
    }
}

static void put_lines(uint8_t lcd_off)
{
    uint8_t k;
    static const uint8_t ids[3] = {NAME_ID, LINE1_ID, LINE2_ID};
    (void)lcd_off;
    for (k = 0; k < 3; k++) {
        hud_raster_list((uint8_t)(k == 0 ? 2u : k - 1u));
        VBK_REG = 1; set_bkg_data(ids[k], 36, (uint8_t *)0xC800u); VBK_REG = 0;
    }
}

/* ---------------------------------------------------------------- result page (胜 / 败)
 * The dialogue page's layout without a speaker: the chapter landscape centred, a 白文 seal (one glyph in
 * paper on cinnabar, 32x32) beside it, the 回纹 divider, then the three lines the caller composes between
 * result_open and result_wait (hud lists 2 = title, 0, 1). */
#define SEAL_ID 169u                     /* bank0 169..184: free in dialogue mode (portrait 1..64, scroll ..168) */
#define SEAL_PAL 2u
#define SEAL_COL 15u
#define SEAL_ROW 1u
static const uint16_t seal_pal[4] = {4312, 24445, 4312, 24445};   /* 朱砂 ground, paper strokes */

void result_open(uint16_t seal_tx) BANKED
{
    uint8_t r, c, z[20], t[16];
    DISPLAY_OFF;                             /* GBDK waits for VBlank itself */
    HIDE_SPRITES; HIDE_WIN;
    SCX_REG = 0; SCY_REG = 0;
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    VBK_REG = 0; set_bkg_data(0, 1, zero16);
    memset(z, 0, 20);
    for (r = 0; r < 18; r++) { VBK_REG = 0; set_bkg_tiles(0, r, 20, 1, z); }
    memset(z, TEXT_PAL, 20);
    for (r = 0; r < 18; r++) { VBK_REG = 1; set_bkg_tiles(0, r, 20, 1, z); }
    VBK_REG = 0;
    {
        uint16_t pal7[4];
        pal7[0] = fs_pals[0]; pal7[1] = fs_pals[1]; pal7[2] = fs_pals[2]; pal7[3] = fs_pals[3];
        set_bkg_palette(TEXT_PAL, 1, pal7);
    }
    put_text_map();
    scroll_panel();
    scroll_at(5);
    /* the seal: raster the glyph into the line buffer (C800: 2 rows x 18 tiles), scale its 16x16 cell x2 */
    hud_begin(); hud_tx(0, seal_tx, 0); hud_raster_list(0);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            uint8_t y, x;
            for (y = 0; y < 8; y++) {
                uint8_t dy = (uint8_t)(r * 8u + y), lo = 0;
                for (x = 0; x < 8; x++) {
                    uint8_t dx = (uint8_t)(c * 8u + x), on = 0;
                    if (dx == 2 || dx == 29 || dy == 2 || dy == 29) on = dx >= 2 && dx <= 29 && dy >= 2 && dy <= 29;
                    else if (dx >= 4 && dx < 28 && dy >= 2 && dy < 30) {
                        uint8_t sx = (uint8_t)((dx - 4u) >> 1), sy = (uint8_t)((dy - 2u) >> 1);
                        const uint8_t *s = (const uint8_t *)0xC800u + ((uint16_t)((sy >> 3) * 18u + (sx >> 3)) << 4) + ((sy & 7u) << 1);
                        on = (uint8_t)(((s[0] | s[1]) >> (7u - (sx & 7u))) & 1u);
                    }
                    if (on) lo |= (uint8_t)(0x80u >> x);
                }
                t[2 * y] = lo; t[2 * y + 1] = 0;
            }
            VBK_REG = 0; set_bkg_data((uint8_t)(SEAL_ID + r * 4u + c), 1, t);
        }
    set_bkg_palette(SEAL_PAL, 1, seal_pal);
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) { z[c] = (uint8_t)(SEAL_ID + r * 4u + c); t[c] = SEAL_PAL; }
        VBK_REG = 1; set_bkg_tiles(SEAL_COL, (uint8_t)(SEAL_ROW + r), 4, 1, t);
        VBK_REG = 0; set_bkg_tiles(SEAL_COL, (uint8_t)(SEAL_ROW + r), 4, 1, z);
    }
}

/* show the composed lines, keep the 40-frame guard in `timer` (tests read it), return J_A or J_SELECT */
uint8_t result_wait(uint8_t select_ok) BANKED
{
    uint8_t prev, k;
    put_lines(1);
    DISPLAY_ON;
    prev = joypad();
    timer = 40;
    for (;;) {
        wait_vbl_done();
        dbg_state = 7; dbg_frame_lo++;         /* G_OUTCOME: the main loop is not running here */
        k = joypad();
        if (timer) timer--;
        else if ((k & J_A) && !(prev & J_A)) { sfx_play(SFX_CONFIRM); break; }
        else if (select_ok && (k & J_SELECT) && !(prev & J_SELECT)) { sfx_play(SFX_CONFIRM); k = J_SELECT; break; }
        prev = k;
    }
    k = (k == J_SELECT) ? J_SELECT : J_A;
    while (joypad() & (J_A | J_SELECT)) wait_vbl_done();
    input_flush();
    DISPLAY_OFF;
    return k;
}
