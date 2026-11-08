#pragma bank 10
#include <gb/gb.h>
#include "vblank.h"
#include "debug.h"

uint16_t vb_slot_addr(uint8_t id) { return id < 128u ? (uint16_t)(0x9000u + (uint16_t)id * 16u) : (uint16_t)(0x8800u + (uint16_t)(id - 128u) * 16u); }

/* wait for the next VBlank and return with LY in 144..149, else keep waiting (LY gate, design doc 12.1) */
void vb_wait(void)
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
uint8_t vb_gdma(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n)
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
void vb_gdma_wait(uint8_t vbk, uint16_t src, uint16_t dst, uint16_t n)
{
    for (;;) {
        vb_wait();
        if (vb_gdma(vbk, src, dst, n)) return;
    }
}

