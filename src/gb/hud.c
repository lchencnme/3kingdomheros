#include <gb/gb.h>
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
#define LINE_GLYPHS 24u
#define LINE_BYTES (2u * LINE_TILES * 16u)   /* 576 */

/* UI arena C800..CBFF (design doc 11.4). Staging must be 16-byte aligned for GDMA. */
__at(0xC800) uint8_t hud_tiles[LINE_BYTES];
__at(0xCA40) uint8_t hud_bits[16 * LINE_TILES];

static uint16_t glyphs[2][LINE_GLYPHS];
static uint8_t nglyph[2];
static uint8_t shown, blank;
static uint8_t job;            /* 0 idle, 1 raster L0, 2 dma L0, 3 raster L1, 4 dma L1, 5 prep ids, 6 flip */
static uint8_t job_page;       /* page being drawn */

volatile uint8_t dbg_hud_busy;

static uint8_t page_base(uint8_t pg) { return pg ? 128u : 0u; }

void hud_begin(void) { nglyph[0] = nglyph[1] = 0; }

static void add(uint8_t line, uint16_t g)
{
    if (nglyph[line] < LINE_GLYPHS) glyphs[line][nglyph[line]++] = g;
}

void hud_num(uint8_t line, uint16_t n)
{
    uint8_t d[5], k = 0;
    do { d[k++] = (uint8_t)(n % 10u); n /= 10u; } while (n && k < 5);
    while (k) add(line, digit_glyph[d[--k]]);
}

void hud_space(uint8_t line) { add(line, GLYPH_SPACE); }
void hud_glyph(uint8_t line, uint16_t g) { add(line, g); }

void hud_tx(uint8_t line, uint8_t tx, uint16_t var)
{
    uint16_t buf[LINE_GLYPHS + 1];
    uint16_t n = (uint16_t)(text_offset[tx + 1] - text_offset[tx]);
    uint8_t i;
    if (n > LINE_GLYPHS + 1) n = LINE_GLYPHS + 1;           /* HUD lines never exceed 24 glyphs */
    rom_copy(TEXT_BANK, buf, &text_pool[text_offset[tx]], (uint16_t)(n * 2u));
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

static void raster_line(uint8_t line)
{
    uint8_t i, x = 0;
    memset(hud_bits, 0, sizeof hud_bits);
    for (i = 0; i < nglyph[line]; i++) {
        uint16_t g = glyphs[line][i];
        uint8_t w = glyph_width[g], col, sh, last;
        uint8_t rows[24];
        if ((uint16_t)x + w > LINE_TILES * 8u) break;
        rom_copy(TEXT_BANK, rows, &glyph_rows[(uint16_t)g * 24u], 24);
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
                *s++ = (c == 0 || c >= 19) ? blank : (uint8_t)(base + (r >> 1) * 36 + (r & 1) * 18 + (c - 1));
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
    if ((job == 2 || job == 4) && ly > 150u) return;
    if (job == 2) { gdma_line(0); job = 3; }
    else if (job == 4) { gdma_line(1); job = 5; }
    else if (job == 6) {
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
}

void hud_init(uint8_t blank_tile)
{
    uint8_t r, c, attr[20];
    blank = blank_tile;
    shown = 1;                       /* first job draws page 0 */
    job = 0;
    /* window attributes never change: bank1 tiles, palette 0 (edge columns: bank0 blank) */
    for (c = 0; c < 20; c++) attr[c] = (c == 0 || c == 19) ? 0 : 0x08;
    VBK_REG = VBK_ATTRIBUTES;
    for (r = 0; r < 4; r++) set_win_tiles(0, r, 20, 1, attr);
    VBK_REG = VBK_TILES;
    /* clear both pages once (LCD is off during init) */
    memset(hud_tiles, 0, LINE_BYTES);
    VBK_REG = VBK_BANK_1;
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
