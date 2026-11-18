#pragma bank 1
/* title cover and the rules pages (moved out of flow.c: bank 14 holds the battle tables of all 52 battles) */
#include "game.h"
#include "../core/save.h"
#include "../gen/title_data.h"
#include "../gen/music_data.h"
void play_pages(const uint16_t (*pages)[2], uint8_t n) BANKED;   /* flow.c */

/* T-RULE-02: the rules a player is most likely to misread, from the cover (B) */
void show_help(void) BANKED
{
    static const uint16_t help_pages[6][2] = {{0xFF, TXD_HELP_1}, {0xFF, TXD_HELP_2}, {0xFF, TXD_HELP_3},
                                               {0xFF, TXD_HELP_4}, {0xFF, TXD_HELP_5}, {0xFF, TXD_HELP_6}};
    {   /* the table is in this bank: queue the pages here, then play (play_pages reads from bank 14) */
        uint8_t i;
        for (i = 0; i < 6; i++) { DQ->speaker[i] = (uint8_t)help_pages[i][0]; DQ->text[i] = help_pages[i][1]; }
        DQ->n = 6;
        play_queue();
    }
    show_title();
}

/* title cover (tools/gen_title.py): a 160x112 picture in BG tiles 0.. of VRAM bank 0 above the two HUD lines
 * with the menu. The main loop leaves the map alone while state == G_TITLE; every way out reloads the map
 * mode (map_mode_restore / the camp). title_opts: bit0 = a suspend save can be continued, bit1 = camp save. */
uint8_t title_opts;
/* the save design needs 32 KiB of cartridge RAM (4 banks, sram_map.h). A smaller cart mirrors the bank
 * number (8 KiB: bank 3 = bank 0, 16 KiB: bank 3 = bank 1), so the runtime area would overwrite the saves and
 * show other officers' names: probe once, and refuse to start a game. */
uint8_t sram_small;
static uint8_t sram_probe_done;
static uint8_t sram_mirrors(void)
{
    uint8_t a0, a1, b, t, r0, r1;
    const uint16_t off = 0x1FF0u;              /* the far end of every bank: restored below */
    sram_read(0, off, &a0, 1); sram_read(1, off, &a1, 1); sram_read(3, off, &b, 1);
    t = (uint8_t)(a0 ^ a1 ^ 0x5Au); if (t == a0 || t == a1) t ^= 0xA5u;
    sram_write(3, off, &t, 1);
    sram_read(0, off, &r0, 1); sram_read(1, off, &r1, 1);
    sram_write(3, off, &b, 1);                 /* restore all three bytes */
    sram_write(1, off, &a1, 1); sram_write(0, off, &a0, 1);
    return (uint8_t)(r0 != a0 || r1 != a1);
}

void show_title(void) BANKED
{
    uint8_t row[TITLE_W], y, copy;
    uint16_t pal[4 * TITLE_PALS];
    uint32_t seq;
    state = G_TITLE;
    autosave_pending = 0;                      /* never overwrite a found save before the choice */
    title_opts = (uint8_t)((save_probe(SAVE_SLOT_SUSPEND, &copy, &seq) == SAVE_OK ? 1u : 0u) | (camp_save_ok ? 2u : 0u));
    DISPLAY_OFF;                               /* GBDK: waits for VBlank, no-op when already off */
    HIDE_SPRITES;
    rom_copy(TITLE_BANK, pal, title_pal, sizeof pal);
    set_bkg_palette(0, TITLE_PALS, pal);       /* ink, 石绿, 朱砂, 赭石: per-tile palettes */
    VBK_REG = VBK_TILES;
    rom_set_bkg_data(TITLE_BANK, 0, 128, title_tiles);
    if (TITLE_TILES > 128u) rom_set_bkg_data(TITLE_BANK, 128, (uint8_t)(TITLE_TILES - 128u), title_tiles + 128u * 16u);
    for (y = 0; y < TITLE_H; y++) {
        rom_copy(TITLE_BANK, row, title_map + (uint16_t)y * TITLE_W, TITLE_W);
        VBK_REG = VBK_TILES; set_bkg_tiles(0, y, TITLE_W, 1, row);
        rom_copy(TITLE_BANK, row, title_attr + (uint16_t)y * TITLE_W, TITLE_W);
        VBK_REG = 1; set_bkg_tiles(0, y, TITLE_W, 1, row);      /* palette per tile, tile bank 0 */
    }
    VBK_REG = 1; set_bkg_data(HUD_BLANK, 1, blank16);
    VBK_REG = VBK_TILES;
    hud_init(HUD_BLANK);                       /* e.g. after the ending pages (dialogue mode) */
    SCX_REG = 0; SCY_REG = 0;
    if (!sram_probe_done) { sram_probe_done = 1; sram_small = sram_mirrors(); }
    hud_begin();
    if (sram_small) {
        hud_tx(0, TX_SRAM_SMALL, 0); hud_tx(1, TX_SRAM_NEED, 0); hud_end();
        if (audio_ready) music_play(&song_title);
        SHOW_BKG; SHOW_WIN;
        DISPLAY_ON;
        return;
    }
    hud_tx(0, TX_TITLE_NEW, 0);
    if (title_opts & 1u) { hud_space(0); hud_space(0); hud_tx(0, TX_TITLE_CONT, 0); }
    if (title_opts & 2u) { hud_tx(1, TX_TITLE_LOAD, 0); hud_space(1); hud_space(1); }
    hud_tx(1, TX_TITLE_HELP, 0);
    hud_end();
    if (audio_ready) music_play(&song_title);
    SHOW_BKG; SHOW_WIN;
    DISPLAY_ON;
}

