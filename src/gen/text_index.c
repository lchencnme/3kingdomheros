#include "text_data.h"
extern const uint8_t glyph_rows_0[];
extern const uint8_t glyph_rows_1[];
extern const uint8_t glyph_rows_2[];
const uint8_t glyph_bank[GLYPH_BANKS] = {2, 18, 30};
const uint8_t *const glyph_ptr[GLYPH_BANKS] = {glyph_rows_0, glyph_rows_1, glyph_rows_2};
extern const uint16_t text_pool_0[];
extern const uint16_t text_pool_1[];
extern const uint16_t text_pool_2[];
extern const uint16_t text_pool_3[];
const uint8_t text_pool_bank[TEXT_POOLS] = {16, 25, 29, 28};
const uint16_t *const text_pool_ptr[TEXT_POOLS] = {text_pool_0, text_pool_1, text_pool_2, text_pool_3};
const uint16_t text_pool_id0[TEXT_POOLS] = {0, 586, 65535, 65535};
const uint16_t text_pool_off0[TEXT_POOLS] = {0, 7578, 65535, 65535};
const uint8_t digit_glyph[10] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint16_t class_name_tx[] = {49, 50, 59, 60, 61, 62, 63, 64, 65, 66, 51, 52, 53, 54, 55, 56, 57, 58};
const uint16_t strat_name_tx[] = {319, 320, 331, 332, 333, 334, 335, 336, 337, 338, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330};
const uint16_t item_name_tx[] = {121, 122, 130, 141, 147, 150, 160, 167, 170, 177, 123, 0, 0, 0, 0, 0, 126, 127, 128, 129, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 142, 143, 144, 145, 146, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 148, 149, 151, 152, 153, 154, 155, 156, 157, 158, 159, 0, 0, 0, 0, 0, 161, 162, 163, 164, 165, 166, 168, 169, 0, 0, 0, 0, 0, 0, 0, 0, 171, 172, 173, 174, 175, 176, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 178, 179, 180, 181, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 124, 125};
const uint16_t terrain_name_tx[] = {341, 342, 343, 344, 345, 346, 347, 348, 349, 350};
