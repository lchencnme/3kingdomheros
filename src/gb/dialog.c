#pragma bank 10
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "dialog.h"
#include "dialogq.h"
#include "hud.h"
#include "rom.h"
#include "input.h"
#include "audio.h"
#include "vblank.h"
#include "debug.h"
#include "../gen/portrait_data.h"
#include "../gen/text_data.h"
#include "../gen/fs_data.h"

/* VRAM: bank0 id 0 blank paper, ids 1..64 portrait; bank1 ids 0..107 = name / line1 / line2 (36 each).
 * Palettes: 0..4 portrait, 7 = paper/ink text. Map 9800; window and sprites off. */
#define TEXT_PAL 7u
#define NAME_ID 0u
#define LINE1_ID 36u
#define LINE2_ID 72u

static const uint8_t zero16[16];

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

/* portrait attrs/palettes are staged in WRAM bank5 (free in dialogue mode), not on the stack */
#define W5_PATTR ((uint8_t *)0xD000u)
#define W5_PPAL ((uint16_t *)0xD040u)

static void show_portrait(uint8_t sp)
{
    uint8_t r, c, ids[8], at[8], o = SVBK_REG;
    if (sp >= PORTRAIT_COUNT) {
        for (r = 1; r < 9; r++) {
            for (c = 0; c < 8; c++) { ids[c] = 0; at[c] = TEXT_PAL; }
            VBK_REG = 1; set_bkg_tiles(1, r, 8, 1, at);
            VBK_REG = 0; set_bkg_tiles(1, r, 8, 1, ids);
        }
        return;
    }
    rom_set_bkg_data(PORTRAIT_BANK, 1, 64, portrait_tiles[sp]);
    SVBK_REG = 5;
    rom_copy(PORTRAIT_BANK, W5_PATTR, portrait_attr[sp], 64);
    rom_copy(PORTRAIT_BANK, W5_PPAL, portrait_pal[sp], PORTRAIT_MAX_PALS * 8u);
    set_bkg_palette(0, PORTRAIT_MAX_PALS, W5_PPAL);
    SVBK_REG = o;
    for (r = 0; r < 8; r++) {
        SVBK_REG = 5;
        for (c = 0; c < 8; c++) { ids[c] = (uint8_t)(1u + r * 8u + c); at[c] = W5_PATTR[r * 8u + c]; }
        SVBK_REG = o;
        VBK_REG = 1; set_bkg_tiles(1, (uint8_t)(1u + r), 8, 1, at);
        VBK_REG = 0; set_bkg_tiles(1, (uint8_t)(1u + r), 8, 1, ids);
    }
}

/* raster the 3 text lines; LCD off: direct copy, LCD on: GDMA in VBlank (design doc 12.3) */
static void put_lines(uint8_t lcd_off)
{
    uint8_t k;
    static const uint8_t ids[3] = {NAME_ID, LINE1_ID, LINE2_ID};
    for (k = 0; k < 3; k++) {
        hud_raster_list((uint8_t)(k == 0 ? 2u : k - 1u));
        if (lcd_off) { VBK_REG = 1; set_bkg_data(ids[k], 36, (uint8_t *)0xC800u); VBK_REG = 0; }
        else vb_gdma_wait(1, 0xC800u, vb_slot_addr(ids[k]), 576);
    }
}

static void wait_a(void)
{
    /* A advances (rising edge); releasing first avoids carrying the confirm press over */
    while (joypad() & J_A) vb_wait();
    while (!(joypad() & J_A)) vb_wait();
    sfx_play(SFX_CURSOR);
}

uint8_t dialog_run_from(uint8_t i) BANKED
{
    DialogQ *q = DQ;
    uint8_t first = 1, last_sp = 0xFD;
    uint16_t pal7[4];
    if (i >= q->n || q->speaker[i] == DQ_DUEL) return i;
    dbg_dialog_active = 1;
    pal7[0] = fs_pals[0]; pal7[1] = fs_pals[1]; pal7[2] = fs_pals[2]; pal7[3] = fs_pals[3];
    for (; i < q->n && q->speaker[i] != DQ_DUEL; i++) {
        uint8_t sp = q->speaker[i], page = 0;
        for (;;) {
            uint8_t more;
            hud_begin();
            if (sp < PORTRAIT_COUNT) hud_tx_list(2, (uint8_t)(TX_PNAME_0 + 0), 0, sp);   /* name list */
            more = hud_tx_page((uint8_t)q->text[i], page);
            if (first || sp != last_sp) {
                /* new speaker: paper, LCD off, reload portrait + text (no tearing, design doc 12.5) */
                vb_wait();
                DISPLAY_OFF;
                HIDE_SPRITES; HIDE_WIN;
                SCX_REG = 0; SCY_REG = 0;
                LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
                if (first) {
                    uint8_t r, z[20];
                    VBK_REG = 0; set_bkg_data(0, 1, zero16);
                    memset(z, 0, 20);
                    for (r = 0; r < 18; r++) { VBK_REG = 0; set_bkg_tiles(0, r, 20, 1, z); }
                    memset(z, TEXT_PAL, 20);
                    for (r = 0; r < 18; r++) { VBK_REG = 1; set_bkg_tiles(0, r, 20, 1, z); }
                    VBK_REG = 0;
                    set_bkg_palette(TEXT_PAL, 1, pal7);
                    put_text_map();
                }
                show_portrait(sp);
                put_lines(1);
                DISPLAY_ON;
                first = 0; last_sp = sp;
            } else {
                put_lines(0);
            }
            dbg_dialog_pages++;
            wait_a();
            if (!more) break;
            page++;
        }
    }
    while (joypad() & J_A) vb_wait();
    input_flush();
    vb_wait();
    DISPLAY_OFF;
    dbg_dialog_active = 0;
    return i;
}
