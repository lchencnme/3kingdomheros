/* YJ4-lite sequencer. Lives in fixed bank 0 (the ISR must never depend on the mapped ROM bank:
 * design doc 11.3) and reads only unbanked song data. Driven by a 64 Hz timer interrupt so that it
 * keeps time while the LCD is off (design doc 14.1). */
#include <gb/gb.h>
#include "debug.h"
#include <gb/hardware.h>
#include "audio.h"

typedef struct {
    const uint8_t *p, *start;
    uint8_t left, gate;
} Voice;

/* all driver state lives in the audio region C200..C3FF (design doc 11.4), not in C globals */
typedef struct {
    const Song *song;
    Voice v[4];
    uint16_t acc;
    uint8_t sfx_left[4], sfx_prio[4];
} AudioState;
#define AS ((AudioState *)0xC200u)
#define song (AS->song)
#define v (AS->v)
#define acc (AS->acc)
#define sfx_left (AS->sfx_left)
#define sfx_prio (AS->sfx_prio)

/* SFX table (design doc 14.3): channel, priority, ticks @64 Hz */
typedef struct { uint8_t ch, prio, ticks, r1, r2, r3, r4; } Sfx;
static const Sfx sfx_tab[] = {
    {1, 20, 3, 0x80, 0x61, 0x00, 0x87},   /* cursor: CH2 short high blip */
    {1, 40, 6, 0x80, 0x82, 0x9D, 0x87},   /* confirm: CH2 two-step */
    {1, 20, 10, 0x40, 0x92, 0x0A, 0x84},  /* error: CH2 low buzz */
    {3, 60, 8, 0x00, 0x92, 0x52, 0x80},   /* hit: CH4 noise */
    {3, 80, 16, 0x00, 0xF3, 0x63, 0x80},  /* retreat: CH4 longer noise */
};

static void ch_off(uint8_t c)
{
    switch (c) {
    case 0: NR12_REG = 0; break;
    case 1: NR22_REG = 0; break;
    case 2: NR30_REG = 0; break;
    default: NR42_REG = 0; break;
    }
}

static void note_on(uint8_t c, uint8_t n)
{
    uint16_t r;
    if (sfx_left[c]) return;                 /* channel borrowed by an SFX: keep time, do not write */
    switch (c) {
    case 0:
        r = mus_pulse_reg[n];
        NR10_REG = 0; NR11_REG = (uint8_t)(song->duty1 << 6);
        NR12_REG = (uint8_t)((song->vol1 << 4) | song->env1);
        NR13_REG = (uint8_t)r; NR14_REG = (uint8_t)(0x80u | (r >> 8));
        break;
    case 1:
        r = mus_pulse_reg[n];
        NR21_REG = (uint8_t)(song->duty2 << 6);
        NR22_REG = (uint8_t)((song->vol2 << 4) | song->env2);
        NR23_REG = (uint8_t)r; NR24_REG = (uint8_t)(0x80u | (r >> 8));
        break;
    case 2:
        r = mus_wave_reg[n];
        NR30_REG = 0x80; NR32_REG = (uint8_t)(song->vol3 << 5);
        NR33_REG = (uint8_t)r; NR34_REG = (uint8_t)(0x80u | (r >> 8));
        break;
    default:
        NR41_REG = 0; NR42_REG = (uint8_t)((n << 4) | song->env4);
        NR43_REG = song->noise4; NR44_REG = 0x80;
        break;
    }
    dbg_audio_notes++;
}

static void step(void)
{
    uint8_t c;
    for (c = 0; c < 4; c++) {
        Voice *x = &v[c];
        if (x->left) {
            x->left--;
            if (x->gate && --x->gate == 0 && !sfx_left[c]) ch_off(c);
            if (x->left) continue;
        }
        if (*x->p == MUS_END) x->p = x->start;
        {
            uint8_t n = x->p[0];
            x->left = x->p[1];
            x->gate = x->p[2];
            x->p += 3;
            if (n == MUS_REST) { if (!sfx_left[c]) ch_off(c); }
            else note_on(c, n);
            x->left--;                         /* this step consumed one tick of the note */
            if (x->gate) x->gate--;
        }
    }
}

static void audio_isr(void) NONBANKED
{
    uint8_t c;
    for (c = 0; c < 4; c++)
        if (sfx_left[c] && --sfx_left[c] == 0) { sfx_prio[c] = 0; ch_off(c); }   /* return channel silent */
    if (!song) return;
    acc += (uint16_t)song->bpm * 24u;        /* 24 ticks per quarter; 64 Hz * 60 s = 3840 */
    while (acc >= 3840u) { acc -= 3840u; step(); dbg_audio_ticks++; }
}

void audio_init(void)
{
    uint8_t i;
    NR52_REG = 0x80; NR50_REG = 0x77; NR51_REG = 0xFF;
    NR30_REG = 0;                             /* load the wave only with the DAC off */
    for (i = 0; i < 16; i++) (&AUD3WAVE[0])[i] = mus_wave_soft[i];
    song = 0;
    /* 64 Hz in double speed: TAC 4096 Hz setting runs at 8192 Hz, TMA = 128 -> 8192 / 128 */
    TMA_REG = 128; TIMA_REG = 128; TAC_REG = 0x04;
    CRITICAL {
        add_TIM(audio_isr);
    }
    set_interrupts(VBL_IFLAG | TIM_IFLAG);
}

void music_play(const Song *s)
{
    uint8_t c;
    CRITICAL {
        for (c = 0; c < 4; c++) { v[c].start = v[c].p = s->ch[c]; v[c].left = 0; v[c].gate = 0; }
        acc = 3840u;                          /* first step on the next tick */
        song = s;
    }
}

void music_stop(void)
{
    uint8_t c;
    CRITICAL { song = 0; for (c = 0; c < 4; c++) ch_off(c); }
}

void sfx_play(uint8_t id)
{
    const Sfx *s = &sfx_tab[id];
    uint8_t c = s->ch;
    if (sfx_left[c] && sfx_prio[c] > s->prio) return;     /* lower priority dropped */
    CRITICAL {
        sfx_left[c] = s->ticks; sfx_prio[c] = s->prio;
        if (c == 1) { NR21_REG = s->r1; NR22_REG = s->r2; NR23_REG = s->r3; NR24_REG = s->r4; }
        else { NR41_REG = s->r1; NR42_REG = s->r2; NR43_REG = s->r3; NR44_REG = s->r4; }
    }
}
