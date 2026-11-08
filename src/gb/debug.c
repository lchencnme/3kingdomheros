/* Test/debug hooks at fixed addresses in the event-queue region tail CDA0..CDFF (design doc 11.4),
 * read by tests/emu through the .noi symbol file. Kept out of the C600..C7FF globals budget. */
#include <stdint.h>
volatile __at(0xCDA0) uint16_t dbg_audio_ticks;
volatile __at(0xCDA2) uint8_t dbg_audio_notes;
volatile __at(0xCDA3) uint8_t dbg_hud_busy;
volatile __at(0xCDA4) uint8_t dbg_state;
volatile __at(0xCDA5) uint8_t dbg_sel;
volatile __at(0xCDA6) uint8_t dbg_cur_x;
volatile __at(0xCDA7) uint8_t dbg_cur_y;
volatile __at(0xCDA8) uint8_t dbg_frame_lo;
volatile __at(0xCDA9) uint8_t dbg_outcome;
volatile __at(0xCDAA) uint8_t dbg_phase;
volatile __at(0xCDAC) uint16_t dbg_cam_x;
volatile __at(0xCDAE) uint16_t dbg_cam_y;
volatile __at(0xCDB0) uint16_t dbg_round;
volatile __at(0xCDB2) uint16_t dbg_t_range;
volatile __at(0xCDB4) uint16_t dbg_t_ai;
volatile __at(0xCDB6) uint8_t dbg_unit_xy[20];
volatile __at(0xCDCA) uint16_t dbg_unit_hp[10];
volatile __at(0xCDDE) uint32_t dbg_rng;
volatile __at(0xCDE2) uint32_t dbg_draws;
volatile __at(0xCDE6) uint8_t dbg_save_result;
volatile __at(0xCDE7) uint8_t dbg_saves;
volatile __at(0xCDE8) uint16_t dbg_ai_t_range;
volatile __at(0xCDEA) uint16_t dbg_ai_t_field;
volatile __at(0xCDEC) uint16_t dbg_ai_t_cand;
volatile __at(0xCDEE) uint8_t dbg_fs_count;
volatile __at(0xCDEF) uint8_t dbg_fs_ly_max;
volatile __at(0xCDF0) uint8_t dbg_fs_skipped;
volatile __at(0xCDF2) uint16_t dbg_fs_frames;
volatile __at(0xCDF4) uint16_t dbg_fs_bytes_max;
volatile __at(0xCDF6) uint8_t dbg_dialog_active;
volatile __at(0xCDF7) uint8_t dbg_dialog_pages;
volatile __at(0xCDF8) uint8_t dbg_vm_err;
volatile __at(0xCDF9) uint8_t dbg_n_units;
