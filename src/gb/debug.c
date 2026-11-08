/* Test/debug hooks at fixed addresses (event region tail, see eventram.h ER_DEBUG), read by tests/emu
 * through the .noi symbol file. Kept out of the C600..C7FF globals budget. */
#include <stdint.h>
volatile __at(0xCDB4) uint16_t dbg_audio_ticks;
volatile __at(0xCDB6) uint8_t dbg_audio_notes;
volatile __at(0xCDB7) uint8_t dbg_hud_busy;
volatile __at(0xCDB8) uint8_t dbg_state;
volatile __at(0xCDB9) uint8_t dbg_sel;
volatile __at(0xCDBA) uint8_t dbg_cur_x;
volatile __at(0xCDBB) uint8_t dbg_cur_y;
volatile __at(0xCDBC) uint8_t dbg_frame_lo;
volatile __at(0xCDBD) uint8_t dbg_outcome;
volatile __at(0xCDBE) uint8_t dbg_phase;
volatile __at(0xCDC0) uint16_t dbg_cam_x;
volatile __at(0xCDC2) uint16_t dbg_cam_y;
volatile __at(0xCDC4) uint16_t dbg_round;
volatile __at(0xCDC6) uint16_t dbg_t_range;
volatile __at(0xCDC8) uint16_t dbg_t_ai;
volatile __at(0xCDCA) uint32_t dbg_rng;
volatile __at(0xCDCE) uint32_t dbg_draws;
volatile __at(0xCDD2) uint8_t dbg_save_result;
volatile __at(0xCDD3) uint8_t dbg_saves;
volatile __at(0xCDD4) uint16_t dbg_ai_t_range;
volatile __at(0xCDD6) uint16_t dbg_ai_t_field;
volatile __at(0xCDD8) uint16_t dbg_ai_t_cand;
volatile __at(0xCDDA) uint8_t dbg_fs_count;
volatile __at(0xCDDB) uint8_t dbg_fs_ly_max;
volatile __at(0xCDDC) uint8_t dbg_fs_skipped;
volatile __at(0xCDDE) uint16_t dbg_fs_frames;
volatile __at(0xCDE0) uint16_t dbg_fs_bytes_max;
volatile __at(0xCDE2) uint8_t dbg_dialog_active;
volatile __at(0xCDE3) uint8_t dbg_dialog_pages;
volatile __at(0xCDE4) uint8_t dbg_vm_err;
volatile __at(0xCDE5) uint8_t dbg_n_units;
volatile __at(0xCDE6) uint8_t dbg_strat_casts;
volatile __at(0xCDE7) uint8_t dbg_menu_item;
