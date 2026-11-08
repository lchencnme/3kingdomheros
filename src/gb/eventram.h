/* Layout of the fixed-WRAM event region CC00..CDFF (design doc 11.4), single source of truth. */
#ifndef GB_EVENTRAM_H
#define GB_EVENTRAM_H
#define ER_DIALOGQ   0xCC00u   /* 64 B  dialogue page queue (dialogq.h) */
#define ER_PAGE_BUF  0xCC40u   /* 128 B text page fetch buffer (hud.c) */
#define ER_LINE_BUF  0xCCC0u   /* 64 B  text line fetch buffer (hud.c) */
#define ER_SLIST     0xCD00u   /* 20 B  known strategy ids (main.c) */
#define ER_TARGETS   0xCD14u   /* 8 B   attack targets (main.c) */
#define ER_GLYPHS    0xCD20u   /* 144 B HUD/dialogue glyph lists 3 x 24 x u16 (hud.c) */
#define ER_NGLYPH    0xCDB0u   /* 3 B   glyph list lengths */
#define ER_DEBUG     0xCDB4u   /* .. 0xCDFF test/debug hooks (debug.c) */
#endif
