/* Undo log (design doc 11.6): while an event script runs, every write it makes is preceded by a log
 * entry with the old bytes. A failing script (BAD_SCRIPT, or the log is full: SCRIPT_UNDO_CAP) is
 * rolled back in reverse order; derived data (occupancy) is rebuilt afterwards.
 * Storage: SRAM15 @1920, 6144 B (the NPC item block 0..1919 sits before it).
 * Entry: space u8, bank u8, offset u16, length u8, old_bytes[length], entry_size u16 (7 B overhead). */
#ifndef CORE_UNDO_H
#define CORE_UNDO_H
#include <stdint.h>
#include "platform.h"

#define UNDO_SRAM_BANK 15u
#define UNDO_SRAM_OFF 1952u      /* 1920 + 32 B header area */
#define UNDO_CAP 6144u

#define US_UNITS 0u              /* WRAM1 active units: offset = byte offset in the unit array */
#define US_CAMPAIGN 1u           /* WRAM1 DF00 campaign area */
#define US_TERRAIN 2u
#define US_CORE 3u               /* platform scalars (gold) */
#define US_SRAM12 4u             /* roster / named / battle flags / AI / prizes */
#define US_SRAM15 5u             /* item block */

void undo_begin(void) CORE_API;
uint8_t undo_active(void) CORE_API;
/* log old bytes before a write; 0 = no room: the caller must NOT perform the write */
uint8_t undo_log(uint8_t space, uint8_t bank, uint16_t off, uint8_t len, const uint8_t *old) CORE_API;
uint8_t undo_overflowed(void) CORE_API;
uint16_t undo_used(void) CORE_API;
void undo_rollback(void) CORE_API;      /* restore everything logged since undo_begin, newest first */
void undo_end(void) CORE_API;           /* commit: forget the log */

/* platform: write old bytes back without logging; rebuild derived data after a rollback */
void undo_restore(uint8_t space, uint8_t bank, uint16_t off, const uint8_t *data, uint8_t len) PLATFORM_API;
void undo_rebuild(void) PLATFORM_API;
#endif
