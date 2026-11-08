/* 《三国豪杰传》 TECH01 v2 skeleton (design doc 16.2 G1, first slice).
 * Implemented: CGB double speed, banked-WRAM grid, scrolling map with dead-zone camera,
 * input repeat, select own unit -> bounded SPFA move range highlight -> legal endpoint commit.
 * Temporary: units are OBJ placeholders (BG compositor pending), no HUD text, no enemy phase. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "../core/path.h"
#include "../core/rules.h"
#include "../core/rng.h"
#include "wram.h"
#include "input.h"
#include "mapview.h"
#include "../gen/tech01_data.h"

#define ST_IDLE 0u
#define ST_PREVIEW 1u

typedef struct {
    uint8_t cls, faction, x, y, acted;
    uint16_t hp, max_hp;
} Unit;   /* temporary: moves to WRAM bank1 40B records with the battle module */

static Unit units[TECH01_UNITS];
static uint8_t cur_x, cur_y, state, sel;
static Rng rng;
static const uint8_t blank16[16];

/* debug/test hooks: fixed addresses read by tests/emu/test_rom.py via the .map file */
volatile uint8_t dbg_state, dbg_sel, dbg_cur_x, dbg_cur_y, dbg_frame_lo;
volatile uint16_t dbg_cam_x, dbg_cam_y;
volatile uint8_t dbg_unit_xy[TECH01_UNITS * 2];
volatile uint16_t dbg_t_derive, dbg_t_range, dbg_t_total;

static uint8_t side_of(uint8_t faction) { return faction == 2 ? SIDE_E : SIDE_A; }

static int8_t unit_at(uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < TECH01_UNITS; i++)
        if (units[i].hp && units[i].x == x && units[i].y == y) return (int8_t)i;
    return -1;
}

static void place_sprites(void)
{
    uint8_t i, s = 0;
    for (i = 0; i < TECH01_UNITS; i++) {
        int16_t sx = (int16_t)units[i].x * 16 - (int16_t)cam_x + 8;
        int16_t sy = (int16_t)units[i].y * 16 - (int16_t)cam_y + 16;
        uint8_t base = units[i].faction == 2 ? OBJT_ENEMY : OBJT_PLAYER;
        uint8_t pal = units[i].faction == 2 ? 1 : (units[i].acted ? 3 : 0);
        set_sprite_tile(s, base); set_sprite_tile(s + 1, (uint8_t)(base + 2));
        set_sprite_prop(s, pal); set_sprite_prop(s + 1, pal);
        if (sx < -8 || sx > 168 || sy < 0 || sy >= (int16_t)VIEW_H + 16) {
            move_sprite(s, 0, 0); move_sprite(s + 1, 0, 0);    /* hide with Y=0 */
        } else {
            move_sprite(s, (uint8_t)sx, (uint8_t)sy);
            move_sprite(s + 1, (uint8_t)(sx + 8), (uint8_t)sy);
        }
        s += 2;
    }
    {
        int16_t sx = (int16_t)cur_x * 16 - (int16_t)cam_x + 8;
        int16_t sy = (int16_t)cur_y * 16 - (int16_t)cam_y + 16;
        set_sprite_tile(s, OBJT_CURSOR); set_sprite_tile(s + 1, OBJT_CURSOR + 2);
        set_sprite_prop(s, 2); set_sprite_prop(s + 1, 2);
        move_sprite(s, (uint8_t)sx, (uint8_t)sy);
        move_sprite(s + 1, (uint8_t)(sx + 8), (uint8_t)sy);
    }
}

static void init_units(void)
{
    uint8_t i;
    StatIn si;
    Derived d;
    memset(&si, 0, sizeof si);
    for (i = 0; i < TECH01_UNITS; i++) {
        const Tech01Unit *t = &tech01_units[i];
        si.cls = t->cls; si.level = 1; si.str = 70; si.intl = 60; si.cmd = 70;
        rules_derive(&si, &d);
        units[i].cls = t->cls; units[i].faction = t->faction;
        units[i].x = t->x; units[i].y = t->y; units[i].acted = 0;
        units[i].hp = d.max_hp; units[i].max_hp = d.max_hp;
        occ_set(t->x, t->y, side_of(t->faction));
    }
}

static void start_preview(uint8_t u)
{
    StatIn si;
    Derived d;
    uint8_t o = SVBK_REG;
    uint16_t t0 = sys_time;
    memset(&si, 0, sizeof si);
    si.cls = units[u].cls; si.level = 1; si.str = 70; si.intl = 60; si.cmd = 70;
    rules_derive(&si, &d);
    dbg_t_derive = sys_time - t0;
    SVBK_REG = WB_SCRATCH;
    path_move_range(W7_MR, units[u].x, units[u].y, d.move,
                    class_defs[units[u].cls].move_type, side_of(units[u].faction));
    SVBK_REG = o;
    dbg_t_range = sys_time - t0;
    sel = u;
    state = ST_PREVIEW;
    mapview_highlight(1);
}

static void end_preview(void)
{
    state = ST_IDLE;
    mapview_highlight(0);
}

static void logic(void)
{
    int8_t u;
    if (in_dx || in_dy) {
        int16_t nx = (int16_t)cur_x + in_dx, ny = (int16_t)cur_y + in_dy;
        if (nx >= 0 && ny >= 0 && nx < (int16_t)grid_w && ny < (int16_t)grid_h) {
            cur_x = (uint8_t)nx; cur_y = (uint8_t)ny;
        }
    }
    if (state == ST_IDLE) {
        if (in_pressed & J_A) {
            u = unit_at(cur_x, cur_y);
            if (u >= 0 && units[u].faction == 0 && !units[u].acted) start_preview((uint8_t)u);
        } else if (in_pressed & J_START) {
            uint8_t i;   /* placeholder end-of-turn: no enemy phase yet */
            for (i = 0; i < TECH01_UNITS; i++) units[i].acted = 0;
        }
    } else if (state == ST_PREVIEW) {
        if (in_pressed & J_B) {
            cur_x = units[sel].x; cur_y = units[sel].y;
            end_preview();
        } else if (in_pressed & J_A) {
            uint8_t ok, o = SVBK_REG;
            SVBK_REG = WB_SCRATCH;
            ok = path_endpoint_ok(W7_MR, cur_x, cur_y);
            SVBK_REG = o;
            if (ok) {
                occ_clear(units[sel].x, units[sel].y);
                units[sel].x = cur_x; units[sel].y = cur_y;
                occ_set(cur_x, cur_y, side_of(units[sel].faction));
                units[sel].acted = 1;
                end_preview();
            }
        }
    }
}

static void publish_debug(void)
{
    uint8_t i;
    dbg_state = state; dbg_sel = sel; dbg_cur_x = cur_x; dbg_cur_y = cur_y;
    dbg_cam_x = cam_x; dbg_cam_y = cam_y;
    for (i = 0; i < TECH01_UNITS; i++) { dbg_unit_xy[i * 2] = units[i].x; dbg_unit_xy[i * 2 + 1] = units[i].y; }
    dbg_frame_lo++;
}

void main(void)
{
    uint8_t i;
    DISPLAY_OFF;
    if (_cpu == CGB_TYPE) cpu_fast();          /* design doc 11.2: double speed */
    rng_seed(&rng, 1);

    set_bkg_palette(0, 8, tech01_bg_pals);
    set_sprite_palette(0, 4, tech01_obj_pals);
    VBK_REG = VBK_TILES;
    set_bkg_data(0, TECH01_BG_TILES, tech01_bg_tiles);
    set_bkg_data(TECH01_BG_TILES, 1, blank16);
    set_sprite_data(0, TECH01_OBJ_TILES, tech01_obj_tiles);

    /* window HUD frame (text pending): blank paper rows at WY=112 */
    for (i = 0; i < 20; i++) {
        set_win_tile_xy(i, 0, TECH01_BG_TILES); set_win_tile_xy(i, 1, TECH01_BG_TILES);
        set_win_tile_xy(i, 2, TECH01_BG_TILES); set_win_tile_xy(i, 3, TECH01_BG_TILES);
    }
    WY_REG = 112; WX_REG = 7;

    wram_init_grid(TECH01_W, TECH01_H, tech01_terrain);
    init_units();
    cur_x = units[0].x; cur_y = units[0].y;
    cam_x = 0; cam_y = (uint16_t)(cur_y * 16u + 8u - 56u);
    mapview_init(&tech01_meta[0][0], tech01_terrain_pal);

    input_init();
    SPRITES_8x16;
    SHOW_BKG; SHOW_WIN; SHOW_SPRITES;
    DISPLAY_ON;

    for (;;) {
        input_update();
        logic();
        mapview_camera_follow(cur_x, cur_y);
        mapview_prepare();
        place_sprites();
        publish_debug();
        wait_vbl_done();
        mapview_commit();
    }
}
