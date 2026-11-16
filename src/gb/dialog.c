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
#include "game.h"
#include "../gen/portrait_data.h"
#include "../gen/scroll_data.h"
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


/* the 青绿山水 scroll beside the portrait (tiles 68..167) and the 回纹 divider under both (tile 168):
 * own palette 1; staged through the HUD line buffer at C800 like the frame tiles */
uint8_t scroll_sel = 0xFF;          /* 0xFF = by the current battle's chapter; flow.c sets SCROLL_END_x for endings */
extern uint8_t cur_bt;
static void scroll_panel(void)
{
    uint8_t ids[20], at[20];
    uint16_t pal[4];
    rom_copy(SCROLL_BANK, pal, scroll_pal, sizeof pal);
    set_bkg_palette(1, 1, pal);
    VBK_REG = 0;
    {   /* the chapter's landscape (or the ending's, when flow.c has set scroll_sel) */
        uint8_t k = scroll_sel;
        const uint8_t *src;
        if (k >= SCROLL_PICS) rom_copy(SCROLL_BANK, &k, &scroll_of_battle[cur_bt], 1);
        rom_copy(SCROLL_BANK, &src, &scroll_ptr[k], sizeof src);
        rom_set_bkg_data(SCROLL_BANK, 68, SCROLL_TILES, src);
    }
    rom_set_bkg_data(SCROLL_BANK, 168, 1, scroll_fret);
    memset(at, 1, 20);
    memset(ids, 168, 20);
    VBK_REG = 1; set_bkg_tiles(0, 10, 20, 1, at);
    VBK_REG = 0; set_bkg_tiles(0, 10, 20, 1, ids);
}

/* the scroll at column col0 of the picture rows (10 beside a portrait, 5 = centred for narration);
 * the rest of those rows is cleared */
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

static void frame(uint8_t on);
static void show_portrait(uint8_t sp)
{
    uint8_t r, c, ids[8], at[8];
    PortraitRef ref;
    uint16_t pal[4];
    if (sp >= PORTRAIT_COUNT) { scroll_at(5); return; }   /* narration: the scroll alone, centred */
    scroll_at(10);
    rom_copy(PORTRAIT_REF_BANK, &ref, &portrait_refs[sp], sizeof ref);   /* 82 portraits over banks 19.. */
    rom_set_bkg_data(ref.bank, 1, 64, ref.tiles);
    rom_copy(PORTRAIT_REF_BANK, pal, portrait_pal + (uint16_t)ref.tint * 4u, sizeof pal);
    set_bkg_palette(0, 1, pal);                                        /* 淡彩: the faction's pigment */
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) { ids[c] = (uint8_t)(1u + r * 8u + c); at[c] = 0; }
        VBK_REG = 1; set_bkg_tiles(1, (uint8_t)(1u + r), 8, 1, at);
        VBK_REG = 0; set_bkg_tiles(1, (uint8_t)(1u + r), 8, 1, ids);
    }
    frame(1);
}

/* double-rule woodcut frame round the 8x8 portrait (tiles 65..67, flipped by attribute for the 4 sides) */
static void frame(uint8_t on)
{
    uint8_t k, id, at;
    /* staged in the HUD line buffer (free here, the text lines are rastered after the portrait): no 48 B on the stack */
    if (on) { rom_copy(PORTRAIT_REF_BANK, (uint8_t *)0xC800u, portrait_frame, 48); VBK_REG = 0; set_bkg_data(65, 3, (uint8_t *)0xC800u); }
    for (k = 0; k < 10; k++) {
        uint8_t edge = k == 0 || k == 9;
        id = on ? (edge ? 65u : 66u) : 0u; at = (uint8_t)(k == 9 ? 0x20u : 0u);           /* top row */
        VBK_REG = 1; set_bkg_tiles(k, 0, 1, 1, &at); VBK_REG = 0; set_bkg_tiles(k, 0, 1, 1, &id);
        at |= 0x40u;                                                                       /* bottom row: Y flip */
        VBK_REG = 1; set_bkg_tiles(k, 9, 1, 1, &at); VBK_REG = 0; set_bkg_tiles(k, 9, 1, 1, &id);
        if (!edge) {
            id = on ? 67u : 0u; at = 0;                                                    /* left / right columns */
            VBK_REG = 1; set_bkg_tiles(0, k, 1, 1, &at); VBK_REG = 0; set_bkg_tiles(0, k, 1, 1, &id);
            at = 0x20u;
            VBK_REG = 1; set_bkg_tiles(9, k, 1, 1, &at); VBK_REG = 0; set_bkg_tiles(9, k, 1, 1, &id);
        }
    }
    VBK_REG = 0;
}

static uint16_t portrait_name(uint8_t sp)
{
    PortraitRef ref;
    rom_copy(PORTRAIT_REF_BANK, &ref, &portrait_refs[sp], sizeof ref);
    return ref.name_tx;
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
            if (sp < PORTRAIT_COUNT) hud_tx(2, portrait_name(sp), 0);          /* the portrait's character name */
            more = hud_tx_page(q->text[i], page);
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
                    scroll_panel();
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
