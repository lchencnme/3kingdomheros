/* Host implementation of unit storage hooks. */
#include <string.h>
#include "../../src/core/battle.h"
#include "../../src/core/grid.h"

extern uint8_t host_occ[4096];
ActiveUnit host_units[MAX_UNITS];
UnitBase host_base[MAX_UNITS];

void unit_load(uint8_t i, ActiveUnit *u) PLATFORM_API { *u = host_units[i]; }
void unit_store(uint8_t i, const ActiveUnit *u) PLATFORM_API { host_units[i] = *u; }
/* officers on the roster take their base stats from the record (roster.c overrides this weak default) */
__attribute__((weak)) uint8_t roster_base(uint16_t roster_offset, UnitBase *b) { (void)roster_offset; (void)b; return 0; }
void unit_base(uint8_t i, UnitBase *b) PLATFORM_API
{
    if (host_units[i].roster_offset != NO_ROSTER && roster_base(host_units[i].roster_offset, b)) return;
    *b = host_base[i];
}
void grid_occ_set(uint8_t x, uint8_t y, uint8_t side) PLATFORM_API { host_occ[(uint16_t)y * grid_w + x] = side; }
void grid_occ_clear(uint8_t x, uint8_t y) PLATFORM_API { host_occ[(uint16_t)y * grid_w + x] = 0; }
