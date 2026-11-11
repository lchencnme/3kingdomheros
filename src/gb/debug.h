/* externs for src/gb/debug.c (do not include from debug.c: SDCC rejects extern + __at pairs) */
#ifndef GB_DEBUG_H
#define GB_DEBUG_H
#include <stdint.h>
extern volatile uint16_t dbg_audio_ticks;
extern volatile uint8_t dbg_audio_notes;
extern volatile uint8_t dbg_hud_busy;
extern volatile uint8_t dbg_state;
extern volatile uint8_t dbg_sel;
extern volatile uint8_t dbg_cur_x;
extern volatile uint8_t dbg_cur_y;
extern volatile uint8_t dbg_frame_lo;
extern volatile uint8_t dbg_outcome;
extern volatile uint8_t dbg_phase;
extern volatile uint16_t dbg_cam_x;
extern volatile uint16_t dbg_cam_y;
extern volatile uint16_t dbg_round;
extern volatile uint16_t dbg_t_range;
extern volatile uint16_t dbg_t_ai;
extern volatile uint32_t dbg_rng;
extern volatile uint32_t dbg_draws;
extern volatile uint8_t dbg_save_result;
extern volatile uint8_t dbg_saves;
extern volatile uint16_t dbg_ai_t_range;
extern volatile uint16_t dbg_ai_t_field;
extern volatile uint16_t dbg_ai_t_cand;
extern volatile uint8_t dbg_fs_count;
extern volatile uint8_t dbg_fs_ly_max;
extern volatile uint8_t dbg_fs_skipped;
extern volatile uint16_t dbg_fs_frames;
extern volatile uint16_t dbg_fs_bytes_max;
extern volatile uint8_t dbg_dialog_active;
extern volatile uint8_t dbg_dialog_pages;
extern volatile uint8_t dbg_vm_err;
extern volatile uint8_t dbg_n_units;
extern volatile uint8_t dbg_strat_casts;
extern volatile uint8_t dbg_menu_item;
extern volatile uint32_t dbg_crc_check;
extern volatile uint16_t dbg_gold;
extern volatile uint8_t dbg_item_uses;
extern volatile uint8_t dbg_ai_strats;
extern volatile uint8_t dbg_ai_items;
extern volatile uint8_t dbg_inv[8];
extern volatile uint8_t dbg_treasures;
extern volatile uint8_t dbg_treasure_mask;
extern volatile uint8_t dbg_battle;
#endif
