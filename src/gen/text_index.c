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
const uint16_t text_pool_id0[TEXT_POOLS] = {0, 627, 949, 65535};
const uint16_t text_pool_off0[TEXT_POOLS] = {0, 7597, 15169, 65535};
const uint8_t digit_glyph[10] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint16_t class_name_tx[] = {75, 76, 85, 86, 87, 88, 89, 90, 91, 92, 77, 78, 79, 80, 81, 82, 83, 84};
const uint16_t strat_name_tx[] = {359, 360, 371, 372, 373, 374, 375, 376, 377, 378, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370};
const uint16_t item_name_tx[] = {151, 152, 160, 171, 177, 180, 190, 197, 200, 207, 153, 0, 0, 0, 0, 0, 156, 157, 158, 159, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 172, 173, 174, 175, 176, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 178, 179, 181, 182, 183, 184, 185, 186, 187, 188, 189, 0, 0, 0, 0, 0, 191, 192, 193, 194, 195, 196, 198, 199, 0, 0, 0, 0, 0, 0, 0, 0, 201, 202, 203, 204, 205, 206, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 208, 209, 210, 211, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 154, 155};
const uint16_t terrain_name_tx[] = {383, 384, 385, 386, 387, 388, 389, 390, 391, 392};
