#pragma bank 5
#include <gb/gb.h>
#include "debug.h"
#include <gb/cgb.h>
#include <string.h>
#include "fs.h"
#include "hud.h"
#include "rom.h"
#include "audio.h"
#include "input.h"
#include "../gen/fs_data.h"
#include "../gen/text_data.h"
#include "../gen/rules_data.h"

/* ---- VRAM plan in fullscreen mode (design doc 12.4)
 * bank0 ids 0..95   plate dictionary          bank0 ids 96..191 composite buffer A
 * bank1 ids 0..95   composite buffer B        bank1 ids 160..231 HUD lines (top / bottom)
 * BG maps 9800 / 9C00 alternate (LCDC bit3); window and sprites off.
 * ---- WRAM plan (banks 5/6 are free while a presentation runs: AI field lives in 3/4)
 * bank5 D000 map ids (18x32), D240 map attrs, D500 plate map copy (260), D600 plate tiles (<=96x16)
 * bank6 D000 pose A (1152), D480 pose B (1152), D900 composite output (<=96x16) */
#define W5_IDS   ((uint8_t *)0xD000u)
#define W5_ATTR  ((uint8_t *)0xD240u)
#define W5_PMAP  ((uint8_t *)0xD500u)
#define W5_PTILE ((uint8_t *)0xD600u)
#define W6_POSEA ((uint8_t *)0xD000u)
#define W6_POSEB ((uint8_t *)0xD480u)
#define W6_OUT   ((uint8_t *)0xD900u)

#define SCENE_ROW0 3u
#define HUD_TOP_ID 160u
#define HUD_BOT_ID 196u


static uint8_t shown_map;          /* 0: 9800 shown, 1: 9C00 shown */
static uint8_t buf_sel;            /* composite buffer for the NEXT frame: 0 = A, 1 = B */
static uint8_t n_out;
static uint8_t skip;

static uint8_t body_of(uint8_t cls)
{
    uint8_t f = class_defs[cls].family;
    if (f == FAM_BOW) return FS_BODY_BOW;
    if (f == FAM_CAVALRY) return FS_BODY_HORSE;
    return FS_BODY_FOOT;
}

static uint8_t pose_index(uint8_t body, uint8_t facing_right, uint8_t pose)
{
    return (uint8_t)((body * 2u + facing_right) * 4u + pose);
}

static uint16_t slot_addr(uint8_t id) { return id < 128u ? (uint16_t)(0x9000u + (uint16_t)id * 16u) : (uint16_t)(0x8800u + (uint16_t)(id - 128u) * 16u); }

/* wait for the next VBlank and return with LY in 144..149, else keep waiting (LY gate, design doc 12.1) */
static void vbl(void)
{
    for (;;) {
        wait_vbl_done();
        if (LY_REG >= 144u && LY_REG <= 149u) return;
    }
}

/* GDMA moves 16 B per 32 dots: scanlines needed for k 16-byte blocks (456 dots per line), rounded up.
 * A table, because a division inside the critical section costs several scanlines on the SM83. */
static const uint8_t lines_for_blocks[65] = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5};
#define lines_for(n) (lines_for_blocks[(uint8_t)((n) >> 4)])

/* start a GDMA only if it ends inside VBlank (LY <= 152). The LY check and the start are atomic:
 * an ISR between them once pushed a 576 B transfer into the visible period (LY 8). Returns 1 if sent. */
static uint8_t gdma(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n)
{
    uint8_t ly, ok = 0;
    CRITICAL {
        ly = LY_REG;
        if (ly >= 144u && (uint16_t)ly + lines_for(n) <= 152u) {
            VBK_REG = vbk;
            HDMA1_REG = (uint8_t)(src >> 8); HDMA2_REG = (uint8_t)(src & 0xF0u);
            HDMA3_REG = (uint8_t)((dst >> 8) & 0x1Fu); HDMA4_REG = (uint8_t)(dst & 0xF0u);
            HDMA5_REG = (uint8_t)(n / 16u - 1u);
            ly = LY_REG;                                     /* end line of the transfer */
            VBK_REG = 0;
            ok = 1;
        }
    }
    if (ok) {
        if (ly < 144u) ly = 154u;                            /* would be a breach (never expected) */
        if (ly > dbg_fs_ly_max) dbg_fs_ly_max = ly;
    }
    return ok;
}

/* send n bytes in one transfer, waiting for VBlanks until it fits */
static void gdma_wait(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n)
{
    for (;;) {
        vbl();
        if (gdma(vbk, src, dst, n)) return;
    }
}

/* ---------------------------------------------------------------- frame composition */
static void compose_actor(uint8_t *pose, uint8_t col0, uint8_t row0, uint8_t pal)
{
    uint8_t k, y;
    for (k = 0; k < FS_ACTOR_TILES; k++) {
        uint8_t c = (uint8_t)(col0 + (k % FS_ACTOR_W)), r = (uint8_t)(row0 + (k / FS_ACTOR_W));
        const uint8_t *src = pose + (uint16_t)k * 16u, *m = pose + 768u + (uint16_t)k * 8u;
        uint8_t any = m[0] | m[1] | m[2] | m[3] | m[4] | m[5] | m[6] | m[7];
        uint8_t bg_id, out_id, slot, bgt[16];
        uint8_t *dst;
        if (!any || c >= 20u || r < SCENE_ROW0 || r >= SCENE_ROW0 + FS_PLATE_H) continue;
        SVBK_REG = 5;
        bg_id = W5_PMAP[(uint16_t)(r - SCENE_ROW0) * 20u + c];
        memcpy(bgt, W5_PTILE + (uint16_t)bg_id * 16u, 16);
        SVBK_REG = 6;
        dst = W6_OUT + (uint16_t)n_out * 16u;
        for (y = 0; y < 8; y++) {
            uint8_t nm = (uint8_t)~m[y];
            dst[2 * y] = (uint8_t)((bgt[2 * y] & nm) | src[2 * y]);
            dst[2 * y + 1] = (uint8_t)((bgt[2 * y + 1] & nm) | src[2 * y + 1]);
        }
        slot = n_out++;
        out_id = buf_sel ? slot : (uint8_t)(96u + slot);
        SVBK_REG = 5;
        W5_IDS[(uint16_t)r * 32u + c] = out_id;
        W5_ATTR[(uint16_t)r * 32u + c] = (uint8_t)(pal | (buf_sel ? 0x08u : 0u));
        SVBK_REG = 6;
    }
}

static void base_map(void)
{
    uint8_t r, c;
    SVBK_REG = 5;
    for (r = 0; r < 18u; r++)
        for (c = 0; c < 32u; c++) {
            uint16_t i = (uint16_t)r * 32u + c;
            uint8_t id = 0, at = 0;
            if (c < 20u) {
                if (r >= SCENE_ROW0 && r < SCENE_ROW0 + FS_PLATE_H) id = W5_PMAP[(uint16_t)(r - SCENE_ROW0) * 20u + c];
                else if ((r <= 1u || r >= 16u) && c >= 1u && c <= 18u) {
                    uint8_t base = r <= 1u ? HUD_TOP_ID : HUD_BOT_ID;
                    id = (uint8_t)(base + (uint8_t)((r & 1u) * 18u) + (uint8_t)(c - 1u));
                    at = 0x08u;                                 /* HUD tiles in VRAM bank1, palette 0 */
                }
            }
            W5_IDS[i] = id; W5_ATTR[i] = at;
        }
}

typedef struct { uint8_t pose_a, col_a, pal_a, pose_b, col_b, pal_b; } Frame;

static void load_pose(uint8_t idx, uint8_t *dst)
{
    uint8_t o = SVBK_REG;
    SVBK_REG = 6;
    rom_copy(fs_pose_bank[idx], dst, fs_pose_ptr[idx], FS_POSE_BYTES);
    SVBK_REG = o;
}

static void build(const Frame *f)
{
    uint8_t o = SVBK_REG;
    load_pose(f->pose_a, W6_POSEA);
    load_pose(f->pose_b, W6_POSEB);
    base_map();
    n_out = 0;
    SVBK_REG = 6;
    compose_actor(W6_POSEA, f->col_a, 6, f->pal_a);
    compose_actor(W6_POSEB, f->col_b, 6, f->pal_b);
    SVBK_REG = o;
}

/* upload the built frame to the hidden map + back buffer, then flip (LCD on: GDMA in VBlank).
 * Design doc 12.1: chunk addresses/lengths are computed in the visible period; VBlank only starts the
 * prepared transfer (an in-VBlank chunk loop once burned ~30 scanlines). One chunk <= 672 B (3 lines). */
#define CHUNK_MAX 672u

static void present(void)
{
    uint8_t o = SVBK_REG, hidden = (uint8_t)(shown_map ^ 1u);
    uint16_t done = 0, total = (uint16_t)n_out * 16u, map = hidden ? 0x9C00u : 0x9800u;
    while (done < total) {
        /* prepare (visible period): contiguous run that does not cross slot id 128 */
        uint8_t first = (uint8_t)(done >> 4), id = buf_sel ? first : (uint8_t)(96u + first);
        uint16_t n = (uint16_t)(total - done), src = (uint16_t)W6_OUT + done, dst = slot_addr(id);
        if (n > CHUNK_MAX) n = CHUNK_MAX;
        if (!buf_sel && id < 128u && (uint16_t)(128u - id) * 16u < n) n = (uint16_t)(128u - id) * 16u;
        SVBK_REG = 6;
        gdma_wait(buf_sel ? 1u : 0u, src, dst, n);
        if (n > dbg_fs_bytes_max) dbg_fs_bytes_max = n;
        done += n;
        dbg_fs_frames++;
    }
    SVBK_REG = 5;
    gdma_wait(0, (uint16_t)W5_IDS, map, 576); dbg_fs_frames++;
    gdma_wait(1, (uint16_t)W5_ATTR, map, 576);
    if (hidden) LCDC_REG |= LCDCF_BG9C00; else LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    dbg_fs_frames++;
    shown_map = hidden;
    buf_sel ^= 1u;
    SVBK_REG = o;
}

/* hold a pose for n frames (A held halves, B skips the presentation) */
static void hold(uint8_t n)
{
    while (n && !skip) {
        uint8_t cur = joypad();
        vbl();
        if (cur & J_B) { skip = 1; dbg_fs_skipped++; }
        if ((cur & J_A) && n > 1) n--;
        n--;
        dbg_fs_frames++;
    }
}

static void hud_line(uint8_t line, uint8_t lcd_off)
{
    hud_raster(line);                                  /* glyphs -> C800 staging (576 B) */
    if (lcd_off) { VBK_REG = 1; set_bkg_data(line ? HUD_BOT_ID : HUD_TOP_ID, 36, (uint8_t *)0xC800u); VBK_REG = 0; }
    else { gdma_wait(1, 0xC800u, slot_addr(line ? HUD_BOT_ID : HUD_TOP_ID), 576); dbg_fs_frames++; }
}

static void paper_palettes(void)
{
    uint16_t p[32];
    uint8_t i;
    for (i = 0; i < 32; i++) p[i] = fs_pals[0];
    set_bkg_palette(0, 8, p);
}

void fs_present(const FsSide *a, const FsSide *d, const AttackResult *r) BANKED
{
    Frame f;
    uint8_t o = SVBK_REG;
    uint8_t ba = body_of(a->cls), bd = body_of(d->cls);
    uint8_t pa = a->fac == FAC_ENEMY ? 2u : 1u, pd = d->fac == FAC_ENEMY ? 2u : 1u;
    skip = 0;
    dbg_fs_count++;
    dbg_fs_frames = 0;

    /* ---- enter: paper palettes, LCD off, load plate + HUD + first frame directly */
    vbl();
    paper_palettes();
    DISPLAY_OFF;
    HIDE_SPRITES; HIDE_WIN;
    SVBK_REG = 5;
    rom_copy(FS_PLATE_BANK, W5_PMAP, fs_plate_map, FS_PLATE_W * FS_PLATE_H);
    rom_copy(FS_PLATE_BANK, W5_PTILE, fs_plate_tiles, (uint16_t)FS_PLATE_TILES * 16u);
    VBK_REG = 0;
    set_bkg_data(0, FS_PLATE_TILES, W5_PTILE);
    SVBK_REG = o;
    hud_begin();
    hud_tx(0, a->name_tx, 0); hud_space(0); hud_num(0, a->hp); hud_space(0); hud_space(0);
    hud_tx(0, d->name_tx, 0); hud_space(0); hud_num(0, d->hp);
    hud_line(0, 1);
    hud_line(1, 1);
    SCX_REG = 0; SCY_REG = 0;
    shown_map = 1; buf_sel = 0;
    f.pose_a = pose_index(ba, 0, FS_POSE_IDLE); f.col_a = 13; f.pal_a = pa;
    f.pose_b = pose_index(bd, 1, FS_POSE_IDLE); f.col_b = 1;  f.pal_b = pd;
    build(&f);
    {   /* LCD off: copy composite + map straight to VRAM */
        uint8_t k;
        SVBK_REG = 6;
        for (k = 0; k < n_out; k++) { VBK_REG = 0; set_bkg_data((uint8_t)(96u + k), 1, W6_OUT + (uint16_t)k * 16u); }
        SVBK_REG = 5;
        VBK_REG = 0; memcpy((uint8_t *)0x9800u, W5_IDS, 576);
        VBK_REG = 1; memcpy((uint8_t *)0x9800u, W5_ATTR, 576);
        VBK_REG = 0;
        SVBK_REG = o;
        shown_map = 0; buf_sel = 1;
    }
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    set_bkg_palette(0, 4, fs_pals);
    DISPLAY_ON;

    /* ---- attack sequence */
    hold(20);
    if (!skip) { f.pose_a = pose_index(ba, 0, FS_POSE_WINDUP); build(&f); present(); hold(12); }
    if (!skip) {
        f.pose_a = pose_index(ba, 0, FS_POSE_STRIKE); f.col_a = 9;
        if (r->hit) f.pose_b = pose_index(bd, 1, FS_POSE_HIT);
        build(&f); present();
        if (r->hit) sfx_play(r->killed ? SFX_RETREAT : SFX_HIT);
        hud_begin();
        if (r->hit) { hud_tx(1, TX_DMG, 0); hud_num(1, r->dmg); } else hud_tx(1, TX_MISS, 0);
        if (r->killed) { hud_space(1); hud_tx(1, TX_RETREAT, 0); }
        hud_line(1, 0);
        hold(24);
    }
    if (!skip) {
        f.pose_a = pose_index(ba, 0, FS_POSE_IDLE); f.col_a = 13;
        f.pose_b = pose_index(bd, 1, FS_POSE_IDLE);
        build(&f); present(); hold(12);
    }
    if (!skip && r->countered) {
        f.pose_b = pose_index(bd, 1, FS_POSE_WINDUP); build(&f); present(); hold(12);
        if (!skip) {
            f.pose_b = pose_index(bd, 1, FS_POSE_STRIKE); f.col_b = 5;
            if (r->c_hit) f.pose_a = pose_index(ba, 0, FS_POSE_HIT);
            build(&f); present();
            if (r->c_hit) sfx_play(r->c_killed ? SFX_RETREAT : SFX_HIT);
            hud_begin();
            hud_tx(1, TX_COUNTER, 0); hud_space(1);
            if (r->c_hit) { hud_tx(1, TX_DMG, 0); hud_num(1, r->c_dmg); } else hud_tx(1, TX_MISS, 0);
            hud_line(1, 0);
            hold(24);
        }
        if (!skip) {
            f.pose_a = pose_index(ba, 0, FS_POSE_IDLE); f.pose_b = pose_index(bd, 1, FS_POSE_IDLE); f.col_b = 1;
            build(&f); present(); hold(12);
        }
    }
    hold(16);
    /* wait for A/B release so the skip press does not leak into the map (design doc 10.2) */
    while (joypad() & (J_A | J_B)) vbl();
    input_flush();

    /* ---- exit: paper, LCD off; the caller restores the map mode */
    vbl();
    paper_palettes();
    DISPLAY_OFF;
    LCDC_REG &= (uint8_t)~LCDCF_BG9C00;
    SVBK_REG = o;
}
