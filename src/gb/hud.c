#include <gb/gb.h>
#include "debug.h"
#include <string.h>
#include "hud.h"
#include "rom.h"
#include "../gen/text_data.h"

/* Non-blocking HUD pipeline (design doc 10.4, 12.1):
 *   hud_end()      -> schedule a job for the back page
 *   hud_service()  -> main loop, visible period: rasterise one line into the GDMA staging buffer
 *   hud_commit()   -> VBlank: GDMA that line (576B) into VRAM bank1, or flip the window map ids
 * The shown page is never written, so there is no tearing; a newer request simply restarts the job. */

#define LINE_TILES 18u
/* text fetch buffers live in the event region (CC40..CCFF) to keep them off the 512 B stack */
#include "eventram.h"
#define PAGE_BUF ((uint16_t *)ER_PAGE_BUF)   /* 64 codes = 128 B */
#define LINE_BUF ((uint16_t *)ER_LINE_BUF)   /* 25 codes = 50 B */

#define LINE_GLYPHS 24u
#define LINE_BYTES (2u * LINE_TILES * 16u)   /* 576 */

/* UI arena C800..CBFF (design doc 11.4). Staging must be 16-byte aligned for GDMA. */
__at(0xC800) uint8_t hud_tiles[LINE_BYTES];
__at(0xCA40) uint8_t hud_bits[16 * LINE_TILES];

/* glyph lists 0/1 = HUD lines (dialogue text), 2 = dialogue name; kept in the event region */
#define GLY(l, i) (((uint16_t *)ER_GLYPHS)[(uint8_t)((l) * LINE_GLYPHS) + (i)])
#define nglyph ((uint8_t *)ER_NGLYPH)
static uint8_t shown, blank;
static uint8_t job;            /* 0 idle, 1 raster L0, 2 dma L0, 3 raster L1, 4 dma L1, 5 prep ids, 6 flip */
static uint8_t job_page;       /* page being drawn */


static uint8_t page_base(uint8_t pg) { return pg ? 128u : 0u; }

void hud_begin(void) { nglyph[0] = nglyph[1] = nglyph[2] = 0; }

static void add(uint8_t line, uint16_t g)
{
    if (nglyph[line] < LINE_GLYPHS) GLY(line, nglyph[line]++) = g;
}

void hud_num(uint8_t line, uint16_t n)
{
    uint8_t d[5], k = 0;
    do { d[k++] = (uint8_t)(n % 10u); n /= 10u; } while (n && k < 5);
    while (k) add(line, digit_glyph[d[--k]]);
}

void hud_space(uint8_t line) { add(line, GLYPH_SPACE); }
void hud_glyph(uint8_t line, uint16_t g) { add(line, g); }

/* offset and length (glyph ids) of string tx: the index lives in its own bank */
static uint16_t text_span(uint16_t tx, uint16_t *off)
{
    uint16_t o[2];
    rom_copy(TEXT_INDEX_BANK, o, &text_offset[tx], 4);
    *off = o[0];
    return (uint16_t)(o[1] - o[0]);
}

/* n glyph ids of string tx from its pool bank (the pool is cut into TEXT_POOLS banks at string boundaries) */
static void text_fetch(uint16_t *buf, uint16_t tx, uint16_t off, uint16_t n)
{
    uint8_t k = 0;
    while (k + 1u < TEXT_POOLS && tx >= text_pool_id0[k + 1u]) k++;
    rom_copy(text_pool_bank[k], buf, text_pool_ptr[k] + (off - text_pool_off0[k]), (uint16_t)(n * 2u));
}

void hud_tx(uint8_t line, uint16_t tx, uint16_t var)
{
    uint16_t *buf = LINE_BUF;
    uint16_t off, n = text_span(tx, &off);
    uint8_t i;
    if (n > LINE_GLYPHS + 1) n = LINE_GLYPHS + 1;           /* HUD lines never exceed 24 glyphs */
    text_fetch(buf, tx, off, n);
    for (i = 0; i < n && buf[i] != TXC_END; i++) {
        if (buf[i] == TXC_VAR) hud_num(line, var);
        else if (buf[i] != TXC_NL) add(line, buf[i]);
    }
}

/* hud_bits is tile-major: tile k (0..35) = 8 bytes, tile row 0 holds pixel rows 0..7 (k = 0..17),
 * tile row 1 pixel rows 8..15 (k = 18..35). Glyph rows sit at pixel rows 2..13. */
#define OR3(D, A, B, C) do { (D)[0] |= (A); (D)[8] |= (B); if (last) (D)[16] |= (C); } while (0)

static void blit_rows(uint8_t *d, const uint8_t *src, uint8_t n, uint8_t sh, uint8_t last)
{
    for (; n; n--, d++, src += 2) {
        uint8_t a = src[0], b = src[1];
        switch (sh) {
        case 0: d[0] |= a; d[8] |= b; break;
        case 2: OR3(d, (uint8_t)(a >> 2), (uint8_t)((a << 6) | (b >> 2)), (uint8_t)(b << 6)); break;
        case 4: OR3(d, (uint8_t)(a >> 4), (uint8_t)((a << 4) | (b >> 4)), (uint8_t)(b << 4)); break;
        default: OR3(d, (uint8_t)(a >> 6), (uint8_t)((a << 2) | (b >> 6)), (uint8_t)(b << 2)); break;
        }
    }
}

/* map-mode info panel: a double rule (ink over the earth pigment) along its top edge, in the two pixel
 * rows above the glyphs; the edge columns carry it through tile blank + 1 */
static void raster_line(uint8_t line)
{
    hud_raster(line);
    if (line == 0) {
        uint8_t k;
        uint8_t *t = hud_tiles;
        for (k = 0; k < LINE_TILES; k++, t += 16) { t[0] = 0xFF; t[1] = 0xFF; t[2] = 0x00; t[3] = 0xFF; }
    }
}

/* page p of a paginated TXD_* string into lists 0 and 1; returns 1 if more pages follow */
uint8_t hud_tx_page(uint16_t tx, uint8_t page)
{
    uint16_t *buf = PAGE_BUF;
    uint16_t off, n = text_span(tx, &off), i;
    uint8_t p = 0, line = 0;
    if (n > 64u) n = 64u;
    text_fetch(buf, tx, off, n);
    for (i = 0; i < n && buf[i] != TXC_END; i++) {
        if (buf[i] == TXC_PAGE) { if (p == page) return 1; p++; line = 0; continue; }
        if (p != page) continue;
        if (buf[i] == TXC_NL) { line = 1; continue; }
        add(line, buf[i]);
    }
    return 0;
}

/* append text tx + k (k selects one of consecutive TX ids) to glyph list 'list' */
void hud_tx_list(uint8_t list, uint16_t tx, uint16_t var, uint8_t k)
{
    hud_tx(list, (uint16_t)(tx + k), var);
}

void hud_raster_list(uint8_t list) { hud_raster(list); }

void hud_raster(uint8_t line)
{
    uint8_t i, x = 0;
    memset(hud_bits, 0, sizeof hud_bits);
    for (i = 0; i < nglyph[line]; i++) {
        uint16_t g = GLY(line, i);
        uint8_t w, col, sh, last, gb = (uint8_t)(g / GLYPH_SPLIT);
        uint8_t rows[24];
        rom_copy(TEXT_INDEX_BANK, &w, &glyph_width[g], 1);
        if ((uint16_t)x + w > LINE_TILES * 8u) break;
        rom_copy(glyph_bank[gb], rows, glyph_ptr[gb] + (uint16_t)(g - (uint16_t)gb * GLYPH_SPLIT) * 24u, 24);
        col = (uint8_t)(x >> 3); sh = (uint8_t)(x & 7);
        last = (uint8_t)(col + 2u < LINE_TILES);
        blit_rows(&hud_bits[(uint16_t)col * 8u + 2u], rows, 6, sh, last);                     /* rows 2..7 */
        blit_rows(&hud_bits[(uint16_t)(col + LINE_TILES) * 8u], rows + 12, 6, sh, last);      /* rows 8..13 */
        x = (uint8_t)(x + w);
    }
    {   /* 1bpp -> 2bpp ink (colour 3): linear duplication */
        uint8_t *o = hud_tiles;
        const uint8_t *s = hud_bits;
        uint16_t n = sizeof hud_bits;
        for (; n; n--) { uint8_t v = *s++; *o++ = v; *o++ = v; }
    }
}

void hud_end(void)
{
    job_page = (uint8_t)(shown ^ 1);
    job = 1;
    dbg_hud_busy = 1;
}

void hud_service(void)
{
    if (job == 1) { raster_line(0); job = 2; }
    else if (job == 3) { raster_line(1); job = 4; }
    else if (job == 5) {
        /* prepare the 4x32 window-map ids for the flip in the staging buffer (visible period) */
        uint8_t r, c, base = page_base(job_page);
        uint8_t *s = hud_tiles;
        for (r = 0; r < 4; r++)
            for (c = 0; c < 32; c++)
                *s++ = (c == 0 || c >= 19) ? (uint8_t)(r == 0 ? blank + 1u : blank)
                                           : (uint8_t)(base + (r >> 1) * 36 + (r & 1) * 18 + (c - 1));
        job = 6;
    }
}

static void gdma_line(uint8_t line)
{
    uint16_t dst = (job_page ? 0x8800u : 0x9000u) + (uint16_t)line * LINE_BYTES;
    VBK_REG = VBK_BANK_1;
    HDMA1_REG = (uint8_t)(0xC800u >> 8);
    HDMA2_REG = 0x00;
    HDMA3_REG = (uint8_t)((dst >> 8) & 0x1F);
    HDMA4_REG = (uint8_t)(dst & 0xF0);
    HDMA5_REG = (uint8_t)(LINE_BYTES / 16u - 1u);   /* bit7 = 0: general-purpose DMA */
    VBK_REG = VBK_BANK_0;
}

void hud_commit(void)
{
    uint8_t ly = LY_REG;
    /* design doc 12.1 LY gate: a 576B GDMA takes ~2.5 lines, the 128B flip < 1 line (VBlank = LY 144..153) */
    if (ly < 144u || ly > 152u) return;
    if ((job == 2 || job == 4) && ly > 149u) return;   /* 576 B ~2.7 lines: must end before line 153 */
    if (job != 2 && job != 4 && job != 6) return;
    CRITICAL {                                 /* LY check + GDMA atomic; the GDMA halts the CPU */
        ly = LY_REG;
        if (ly < 144u || ly > 152u || ((job == 2 || job == 4) && ly > 149u)) ly = 0;
        else if (job == 2) { gdma_line(0); job = 3; }
        else if (job == 4) { gdma_line(1); job = 5; }
        else {
            /* flip: GDMA the prepared 4 rows x 32 window-map ids (128B), atomic and short */
            VBK_REG = VBK_TILES;
            HDMA1_REG = (uint8_t)(0xC800u >> 8);
            HDMA2_REG = 0x00;
            HDMA3_REG = (uint8_t)((0x9C00u >> 8) & 0x1F);
            HDMA4_REG = 0x00;
            HDMA5_REG = (uint8_t)(128u / 16u - 1u);
            shown = job_page;
            job = 0;
            dbg_hud_busy = 0;
        }
        if (ly && (STAT_REG & 3u) != 1u) dbg_vram_late++;   /* 12.1 test hook: still inside VBlank? */
    }
}

void hud_init(uint8_t blank_tile)
{
    uint8_t r, c, attr[20];
    blank = blank_tile;
    shown = 1;                       /* first job draws page 0 */
    job = 0;
    /* window attributes never change: bank1 tiles, palette 0 */
    for (c = 0; c < 20; c++) attr[c] = 0x08;   /* edge columns too: the blank tile lives in bank1 */
    VBK_REG = VBK_ATTRIBUTES;
    for (r = 0; r < 4; r++) set_win_tiles(0, r, 20, 1, attr);
    VBK_REG = VBK_TILES;
    /* clear both pages once (LCD is off during init) */
    memset(hud_tiles, 0, LINE_BYTES);
    VBK_REG = VBK_BANK_1;
    hud_tiles[0] = 0xFF; hud_tiles[1] = 0xFF; hud_tiles[3] = 0xFF;      /* edge tile: the same double rule */
    set_bkg_data((uint8_t)(blank + 1u), 1, hud_tiles);
    hud_tiles[0] = 0; hud_tiles[1] = 0; hud_tiles[3] = 0;
    set_bkg_data(0, 36, hud_tiles); set_bkg_data(36, 36, hud_tiles);
    set_bkg_data(128, 36, hud_tiles); set_bkg_data(164, 36, hud_tiles);
    VBK_REG = VBK_BANK_0;
    hud_begin();
    /* synchronous first page while the LCD is off */
    job_page = 0;
    raster_line(0); VBK_REG = VBK_BANK_1; set_bkg_data(0, 36, hud_tiles); VBK_REG = VBK_BANK_0;
    raster_line(1); VBK_REG = VBK_BANK_1; set_bkg_data(36, 36, hud_tiles); VBK_REG = VBK_BANK_0;
    job = 5;
    hud_service();
    {   /* LCD is off: copy ids directly */
        uint8_t r;
        VBK_REG = VBK_TILES;
        for (r = 0; r < 4; r++) memcpy((uint8_t *)0x9C00 + r * 32, hud_tiles + r * 32, 32);
    }
    shown = 0; job = 0; dbg_hud_busy = 0;
}
