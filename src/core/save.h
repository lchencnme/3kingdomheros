/* Save images (design doc 11.5/11.6): 32B header + TLV body, two copies, CRC32, commit markers.
 * The body is streamed straight to SRAM (no 16 KiB RAM buffer exists). */
#ifndef CORE_SAVE_H
#define CORE_SAVE_H
#define SAVE_MAX_DEPLOY 8u   /* deploy entries in a snapshot (tag 8): dep buffers are 1 + 4 x this */
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
#define SAVE_SLOT_SNAPSHOT 3u  /* SRAM banks 13 / 14 (8 KiB copies): pre-battle snapshot (11.6) */

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
#define SAVE_KIND_BATTLE 1u      /* core @43 save_kind; battle (suspend) saves: tags 1-7, 9, 10 */
#define SAVE_KIND_CAMP 2u
#define SAVE_KIND_SNAPSHOT 3u    /* pre-battle snapshot: tags 1, 2, 3, 7, 8 (deployment) */        /* camp saves: tags 1, 2, 3, 7 (roster, named, campaign, core) */
/* platform: the 256 B campaign area (global flags; GB: WRAM1 DF00) */
void campaign_read(uint8_t off, uint8_t *dst, uint8_t n) PLATFORM_API;
void campaign_write(uint8_t off, const uint8_t *src, uint8_t n) PLATFORM_API;
/* camp save (2.2): roster, named states, campaign area, gold and the next battle (ui->battle_id) */
uint8_t save_camp(uint8_t slot, const SaveUi *ui) CORE_API;
/* 11.6 pre-battle snapshot, written before BATTLE_START: roster, named states, campaign area, core
 * (battle id, starting round, gold), deployment dep = n + n x {officer, x, y, 0} (n <= 16). Retrying a
 * battle restores it, so nothing the lost battle changed (flags, gold, prizes) survives. */
uint8_t save_snapshot(const Battle *b, const SaveUi *ui, const uint8_t *dep) CORE_API;
uint8_t load_snapshot(Battle *b, SaveUi *ui, uint8_t *dep) CORE_API;
uint8_t load_camp(uint8_t slot, SaveUi *ui) CORE_API;
/* battle id stored in a slot's core (to set up the right map before load_battle), 0xFFFF if none */
uint16_t save_peek_battle(uint8_t slot) CORE_API;
/* kind of the newest valid copy found by the last probe/save of slot, 0xFF if none */
uint8_t save_kind(uint8_t slot) CORE_API;
/* forget cached probe results (tests that modify SRAM behind the save module's back) */
void save_cache_reset(void) CORE_API;
/* invalidate both copies of a slot (clear commit markers), e.g. suspend after RESULT */
void save_invalidate(uint8_t slot) CORE_API;
#endif
