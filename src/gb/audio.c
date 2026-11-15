/* YJ4-lite sequencer. Lives in fixed bank 0 (the ISR must never depend on the mapped ROM bank:
 * design doc 11.3); song data is in ROM bank 17, mapped by the ISR for the duration of a tick. Driven by a 64 Hz timer interrupt so that it
 * keeps time while the LCD is off (design doc 14.1). */
#include <gb/gb.h>
#include "debug.h"
#include <gb/hardware.h>
#include "audio.h"

typedef struct {
    const uint8_t *p, *start;
    uint8_t left, gate;
} Voice;

/* all driver state lives in the audio region C200..C27F (design doc 11.4; C280..C39F: unit snapshot), not in C globals */
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
    {1, 40, 6, 0x80, 0x82, 0x6B, 0x87},   /* confirm: CH2 ~880 Hz (was 1.3 kHz: shrill) */
    {1, 20, 10, 0x40, 0x92, 0x0A, 0x84},  /* error: CH2 low buzz */
    {3, 60, 8, 0x00, 0x92, 0x52, 0x80},   /* hit: CH4 noise */
    {3, 80, 16, 0x00, 0xF3, 0x63, 0x80},  /* retreat: CH4 longer noise */
};

/* silence a channel without switching its DAC off: a DAC on/off step is an audible pop on every rest.
 * NRx2 = 0x08 keeps the DAC on at volume 0; the wave channel is muted with its output level instead. */
static void ch_off(uint8_t c)
{
    switch (c) {
    case 0: NR12_REG = 0x08; NR14_REG = 0x80; break;
    case 1: NR22_REG = 0x08; NR24_REG = 0x80; break;
    case 2: NR32_REG = 0; break;
    default: NR42_REG = 0x08; NR44_REG = 0x80; break;
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
        if (*x->p == MUS_EOT) x->p = x->start;
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
    uint8_t c, saved;
    for (c = 0; c < 4; c++)
        if (sfx_left[c] && --sfx_left[c] == 0) { sfx_prio[c] = 0; ch_off(c); }   /* return channel silent */
    if (!song) return;
    /* song data is in ROM bank 17: map it, read, restore the interrupted code's bank (_current_bank is
     * always written before the MBC register by the banked-call trampoline, so this is consistent) */
    saved = _current_bank;
    SWITCH_ROM(MUSIC_BANK);
    acc += (uint16_t)song->bpm * 24u;        /* 24 ticks per quarter; 64 Hz * 60 s = 3840 */
    while (acc >= 3840u) { acc -= 3840u; step(); dbg_audio_ticks++; }
    SWITCH_ROM(saved);
}

void audio_init(void)
{
    uint8_t i;
    NR52_REG = 0x80; NR50_REG = 0x66; NR51_REG = 0xFF;
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

uint8_t cur_bgm;                         /* the current battle's music (BattleDef.bgm or a script's BGM) */
void music_play_id(uint8_t id) { if (id < MUS_COUNT) music_play(mus_songs[id]); }

void music_play(const Song *s)
{
    uint8_t c;
    CRITICAL {
        uint8_t saved = _current_bank;
        SWITCH_ROM(MUSIC_BANK);               /* the Song record is in bank 17 */
        for (c = 0; c < 4; c++) { v[c].start = v[c].p = s->ch[c]; v[c].left = 0; v[c].gate = 0; }
        SWITCH_ROM(saved);
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
