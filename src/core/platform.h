/* Calling-convention shim: on GB the core lives in code bank 3 and its public API is BANKED
 * (design doc 11.3); on the host the macro is empty so the same sources compile natively. */
#ifndef CORE_PLATFORM_H
#define CORE_PLATFORM_H
#ifdef __SDCC
#include <gb/gb.h>
#define CORE_API BANKED        /* core public API: code banks 3 (rng/rules/path) and 4 (battle/ai) */
#define PLATFORM_API NONBANKED /* platform hooks called by the core: must live in fixed bank 0 */
#define SVBK_SAVE(v) uint8_t v = SVBK_REG
#define SVBK_RESTORE(v) (SVBK_REG = (v))
#else
#define CORE_API
#define PLATFORM_API
#define SVBK_SAVE(v) uint8_t v = 0
#define SVBK_RESTORE(v) ((void)(v))
#endif
#endif
