/* Cartridge RAM map: 32 KiB (4 banks x 8 KiB), MBC5 header RAM size 0x03.
 * Earlier builds needed 128 KiB; the real data is far smaller (a camp save ~1.7 KiB, a battle save ~4.6 KiB,
 * 10 officers), so every save keeps its two copies (11.6) in 32 KiB:
 *
 *   bank 0  0x0000 camp save copy A   (2 KiB)     bank 1  suspend (battle) save copy A (8 KiB)
 *           0x0800 camp save copy B   (2 KiB)     bank 2  suspend (battle) save copy B (8 KiB)
 *           0x1000 snapshot copy A    (2 KiB)     bank 3  runtime work area (below)
 *           0x1800 snapshot copy B    (2 KiB)
 *
 * Images are addressed as one 15-bit SRAM address (bank * 8192 + offset). */
#ifndef CORE_SRAM_MAP_H
#define CORE_SRAM_MAP_H

#define SRAM_BANKS 4u

#define SRAM_CAMP_A 0x0000u
#define SRAM_CAMP_B 0x0800u
#define SRAM_SNAP_A 0x1000u
#define SRAM_SNAP_B 0x1800u
#define SRAM_SMALL_COPY 0x0800u          /* camp / snapshot copy size */
#define SRAM_SUSP_A 0x2000u
#define SRAM_SUSP_B 0x4000u
#define SRAM_BIG_COPY 0x2000u            /* suspend copy size */

/* bank 3: runtime work area (what the battle and the camp read and write; saves copy it) */
#define RT_BANK 3u
#define RT_ROSTER_OFF 0u                 /* 16 records x 48 B */
#define RT_ROSTER_MAX 16u
#define RT_NAMED_OFF 768u                /* named states 1024 x 2 bit */
#define RT_FLAGS_OFF 1024u               /* battle flags 512 B */
#define RT_AI_GROUPS 1536u               /* AI groups 96 x 5 B */
#define RT_AI_INST 2016u                 /* AI instance overrides 96 x 5 B */
#define RT_PRIZE_OFF 2496u               /* prize queue: count + 16 item ids */
#define RT_ROSTER_COUNT 2514u
#define RT_INV_OFF 2560u                 /* bags: 24 units x 20 B */
#define RT_INV_UNITS 24u
#define RT_UNDO_OFF 3072u                /* script undo log up to the end of the bank */
#define RT_UNDO_CAP 5120u

typedef char sram_map_fits[(RT_UNDO_OFF + RT_UNDO_CAP <= 8192u && RT_INV_OFF + RT_INV_UNITS * 20u <= RT_UNDO_OFF
                            && RT_ROSTER_OFF + RT_ROSTER_MAX * 48u <= RT_NAMED_OFF && RT_AI_INST + 480u <= RT_PRIZE_OFF) ? 1 : -1];
#endif
