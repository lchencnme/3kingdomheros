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
const uint16_t text_pool_id0[TEXT_POOLS] = {0, 608, 914, 65535};
const uint16_t text_pool_off0[TEXT_POOLS] = {0, 7591, 15181, 65535};
const uint8_t digit_glyph[10] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint16_t class_name_tx[] = {75, 76, 85, 86, 87, 88, 89, 90, 91, 92, 77, 78, 79, 80, 81, 82, 83, 84};
const uint16_t strat_name_tx[] = {349, 350, 361, 362, 363, 364, 365, 366, 367, 368, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360};
const uint16_t item_name_tx[] = {147, 148, 156, 167, 173, 176, 186, 193, 196, 203, 149, 0, 0, 0, 0, 0, 152, 153, 154, 155, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 168, 169, 170, 171, 172, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 174, 175, 177, 178, 179, 180, 181, 182, 183, 184, 185, 0, 0, 0, 0, 0, 187, 188, 189, 190, 191, 192, 194, 195, 0, 0, 0, 0, 0, 0, 0, 0, 197, 198, 199, 200, 201, 202, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 204, 205, 206, 207, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 150, 151};
const uint16_t terrain_name_tx[] = {371, 372, 373, 374, 375, 376, 377, 378, 379, 380};
