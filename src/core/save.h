/* Save images (design doc 11.5/11.6): 32B header + TLV body, two copies, CRC32, commit markers.
 * The body is streamed straight to SRAM (no 16 KiB RAM buffer exists). */
#ifndef CORE_SAVE_H
#define CORE_SAVE_H
#include <stdint.h>
#include "platform.h"
#include "battle.h"

#define SAVE_MAGIC0 'Y'
#define SAVE_SCHEMA 2u
#define SAVE_IMAGE_BYTES 16384u
#define SAVE_MAX_BODY 16352u

#define SAVE_SLOT_NORMAL0 0u   /* SRAM banks 0..3  */
#define SAVE_SLOT_NORMAL1 1u   /* SRAM banks 4..7  */
#define SAVE_SLOT_SUSPEND 2u   /* SRAM banks 8..11 */

#define SAVE_OK 0u
#define SAVE_ERR_NONE 1u        /* no valid copy */
#define SAVE_ERR_IO 2u          /* readback mismatch */
#define SAVE_ERR_PROFILE 3u
#define SAVE_ERR_SCHEMA 4u
#define SAVE_ERR_CONFLICT 5u      /* both copies valid, sequence delta exactly 0x80000000 */

/* platform: byte-addressed SRAM access; must enable RAM only for the duration of the call */
void sram_write(uint8_t bank, uint16_t off, const uint8_t *src, uint16_t n) PLATFORM_API;
void sram_read(uint8_t bank, uint16_t off, uint8_t *dst, uint16_t n) PLATFORM_API;

uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint16_t n) CORE_API;

/* UI fields stored in core512 (design doc 附录B.3) */
typedef struct {
    uint16_t battle_id, cam_x, cam_y;
    uint8_t cur_x, cur_y, presentation, text_speed, options;
    uint32_t gold;
} SaveUi;

/* write a battle (suspend) save of the current battle into slot; returns SAVE_OK or SAVE_ERR_IO */
uint8_t save_battle(uint8_t slot, const Battle *b, const SaveUi *ui) CORE_API;
/* validate both copies of slot; returns SAVE_OK and the chosen copy (0/1) + its sequence */
uint8_t save_probe(uint8_t slot, uint8_t *copy, uint32_t *seq) CORE_API;
/* load the newest valid battle save: units via unit_store/grid_occ_set, battle + ui filled.
 * turn_limit is not saved (it comes from the battle definition): the caller sets it. */
uint8_t load_battle(uint8_t slot, Battle *b, SaveUi *ui) CORE_API;
/* forget cached probe results (tests that modify SRAM behind the save module's back) */
void save_cache_reset(void) CORE_API;
/* invalidate both copies of a slot (clear commit markers), e.g. suspend after RESULT */
void save_invalidate(uint8_t slot) CORE_API;
#endif
