/* YJ4-lite 4-channel sequencer (design doc 14): 64 Hz timer interrupt, tempo accumulator, SFX priority. */
#ifndef GB_AUDIO_H
#define GB_AUDIO_H
#include <stdint.h>
#include "../gen/music_data.h"

#define SFX_CURSOR 0u
#define SFX_CONFIRM 1u
#define SFX_ERROR 2u
#define SFX_HIT 3u
#define SFX_RETREAT 4u

void audio_init(void);
void music_play(const Song *s);
void music_play_id(uint8_t id);          /* MUS_* (src/gen/music_data.h) */
extern uint8_t cur_bgm;                  /* the battle's music: restored after a duel / presentation */
void music_stop(void);
void sfx_play(uint8_t id);
#endif
