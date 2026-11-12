#ifdef __SDCC
#pragma bank 8
#endif
#include "vm.h"
#include "ai.h"
#include "item.h"
#include "undo.h"
#include "grid.h"
#include "path.h"

uint8_t vm_last_ran, vm_action_replaced, vm_units_changed;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static uint8_t cmp(uint16_t a, uint8_t op, uint16_t b)
{
    switch (op) {
    case CMP_EQ: return a == b;
    case CMP_NE: return a != b;
    case CMP_LT: return a < b;
    case CMP_LE: return a <= b;
    case CMP_GT: return a > b;
    default: return a >= b;
    }
}

/* evaluate the condition tree at *off (prefix encoding); advances *off; depth <= 8 (compiler-checked) */
static uint8_t eval(const Battle *b, const VmFact *f, uint16_t *off, uint8_t depth)
{
    uint8_t h[6], r = 0, k, n;
    ActiveUnit u;
    if (depth > 8) return 0;
    vm_read(*off, h, 1);
    switch (h[0]) {
    case CN_ALL:
    case CN_ANY:
        vm_read((uint16_t)(*off + 1u), &n, 1);
        *off += 2;
        r = h[0] == CN_ALL;
        for (k = 0; k < n; k++) {
            uint8_t v = eval(b, f, off, (uint8_t)(depth + 1));
            if (h[0] == CN_ALL) r = (uint8_t)(r && v); else r = (uint8_t)(r || v);
        }
        return r;
    case CN_NOT:
        *off += 1;
        return (uint8_t)!eval(b, f, off, (uint8_t)(depth + 1));
    case CN_FLAG_EQ:
        vm_read((uint16_t)(*off + 1u), h, 4); *off += 5;
        return vm_flag_get(h[0], rd16(h + 1)) == h[3];
    case CN_TURN_CMP:
        vm_read((uint16_t)(*off + 1u), h, 3); *off += 4;
        return cmp(b->round, h[0], rd16(h + 1));
    case CN_UNIT_ALIVE:
        vm_read((uint16_t)(*off + 1u), h, 1); *off += 2;
        if (h[0] >= b->n_units) return 0;          /* not instantiated yet (design doc 9.2) */
        unit_load(h[0], &u);
        return unit_alive(&u);
    case CN_UNIT_IN_RECT:
        vm_read((uint16_t)(*off + 1u), h, 5); *off += 6;
        if (h[0] >= b->n_units) return 0;
        unit_load(h[0], &u);
        return unit_alive(&u) && u.x >= h[1] && u.y >= h[2] && u.x <= h[3] && u.y <= h[4];
    case CN_ACTOR_EQ:
        vm_read((uint16_t)(*off + 1u), h, 1); *off += 2;
        return f->actor == h[0];
    case CN_TARGET_EQ:
        vm_read((uint16_t)(*off + 1u), h, 1); *off += 2;
        return f->target == h[0];
    case CN_PHASE_EQ:
        vm_read((uint16_t)(*off + 1u), h, 1); *off += 2;
        return f->phase == h[0];
    default:
        *off = 0xFFFFu;                            /* malformed: caller reports BAD_SCRIPT */
        return 0;
    }
}

static uint8_t cond_true(const Battle *b, const VmFact *f, uint16_t off)
{
    if (off == 0xFFFFu) return 1;
    return eval(b, f, &off, 0);
}

/* SPAWN placement (design doc 9.3): requested cell if free and standable, else the nearest free
 * standable cell by Manhattan distance, then (y, x). 0 = no free cell (pending queue not in G1). */
static uint8_t spawn_cell(uint8_t mtype, uint8_t *x, uint8_t *y)
{
    uint8_t r, best_d = 255, bx = 0, by = 0, cx = *x, cy = *y, found = 0;
    for (r = 0; r < 64 && !found; r++) {
        int16_t dy;
        for (dy = -(int16_t)r; dy <= (int16_t)r; dy++) {
            int16_t yy = (int16_t)cy + dy, dxm = (int16_t)r - (dy < 0 ? -dy : dy), s;
            for (s = -1; s <= 1; s += 2) {
                int16_t xx = (int16_t)cx + s * dxm;
                if (yy < 0 || xx < 0 || yy >= grid_h || xx >= grid_w) { if (!dxm) break; continue; }
                if (path_cost((uint8_t)xx, (uint8_t)yy, mtype) != 255 && grid_occ((uint8_t)xx, (uint8_t)yy) == 0) {
                    uint8_t d = r;
                    if (!found || d < best_d || (d == best_d && ((uint16_t)yy * grid_w + xx) < ((uint16_t)by * grid_w + bx))) {
                        best_d = d; bx = (uint8_t)xx; by = (uint8_t)yy; found = 1;
                    }
                }
                if (!dxm) break;
            }
        }
    }
    *x = bx; *y = by;
    return found;
}

static uint8_t run_code(Battle *b, const VmFact *f, uint16_t off, uint16_t len)
{
    uint16_t end = (uint16_t)(off + len), steps = 0;
    uint8_t a[6];
    ActiveUnit u;
    while (off < end) {
        uint8_t op;
        if (++steps > 4096u) return VM_BUDGET;      /* design doc 9.3 transaction budget */
        vm_read(off, &op, 1);
        switch (op) {
        case OP_END:
            return VM_OK;
        case OP_TEXT:
            vm_read((uint16_t)(off + 1u), a, 3);
            vm_text(a[0], rd16(a + 1));
            off += 4;
            break;
        case OP_SET_FLAG:
            vm_read((uint16_t)(off + 1u), a, 4);
            vm_flag_set(a[0], rd16(a + 1), a[3]);
            off += 5;
            break;
        case OP_IF:
            vm_read((uint16_t)(off + 1u), a, 4);
            off += 5;
            if (!cond_true(b, f, rd16(a))) off = (uint16_t)(off + rd16(a + 2));   /* forward only */
            break;
        case OP_SPAWN: {
            VmTemplate t;
            uint8_t x, y;
            vm_read((uint16_t)(off + 1u), a, 3);
            off += 4;
            if (!vm_template(a[0], &t) || t.instance_id >= MAX_UNITS) return VM_BAD_SCRIPT;
            if (t.instance_id < b->n_units) {        /* slot already instantiated: never recycled (9.3) */
                unit_load(t.instance_id, &u);
                if (u.max_hp) return VM_BAD_SCRIPT;
            }
            x = a[1]; y = a[2];
            if (!spawn_cell(class_defs[t.cls].move_type, &x, &y)) break;   /* G1: no pending queue yet */
            battle_spawn(b, t.instance_id, t.character, t.cls, t.faction, t.level, x, y);
            vm_units_changed = 1;
            break;
        }
        case OP_RETREAT:
            vm_read((uint16_t)(off + 1u), a, 1);
            off += 2;
            if (a[0] < b->n_units) {
                unit_load(a[0], &u);
                if (unit_alive(&u)) {
                    grid_occ_clear(u.x, u.y);
                    u.hp = 0; u.action_flags |= AF_RETREAT;
                    unit_store(a[0], &u);
                    vm_units_changed = 1;
                }
            }
            break;
        case OP_WEATHER:
            vm_read((uint16_t)(off + 1u), a, 1);
            b->weather = a[0] ? 1u : 0u;
            off += 2;
            break;
        case OP_OUTCOME:
            vm_read((uint16_t)(off + 1u), a, 1);
            if (b->outcome == OUT_NONE) b->outcome = a[0] == OUT_WIN ? OUT_WIN : OUT_LOSE;
            off += 2;
            break;
        case OP_GIVE_GOLD:
            vm_read((uint16_t)(off + 1u), a, 2);
            vm_gold(rd16(a));
            off += 3;
            break;
        case OP_HP_SET:
            vm_read((uint16_t)(off + 1u), a, 3);
            off += 4;
            if (a[0] < b->n_units) {
                unit_load(a[0], &u);
                if (unit_alive(&u)) {
                    uint16_t v = rd16(a + 1);
                    if (v > u.max_hp) v = u.max_hp;
                    if (v == 0) { grid_occ_clear(u.x, u.y); u.action_flags |= AF_RETREAT; vm_units_changed = 1; }
                    u.hp = v;
                    unit_store(a[0], &u);
                }
            }
            break;
        case OP_GIVE_ITEM:
            vm_read((uint16_t)(off + 1u), a, 1);
            off += 2;
            if (!prize_push(a[0])) return VM_BAD_SCRIPT;   /* queue full / unknown id: compiler should prevent */
            break;
        case OP_SET_AI:
            vm_read((uint16_t)(off + 1u), a, 6);
            off += 7;
            if (a[2] > AI_ESCAPE) return VM_BAD_SCRIPT;
            ai_set(rd16(a), a[2], rd16(a + 3), a[5]);
            break;
        case OP_DUEL: {
            /* design doc 9.3: named duel replaces this attack, consumes the action, draws no RNG */
            uint16_t lhp;
            uint8_t retreated = 0;
            vm_read((uint16_t)(off + 1u), a, 4);
            off += 5;
            if (a[0] >= b->n_units || a[1] >= b->n_units) return VM_BAD_SCRIPT;
            unit_load(a[1], &u);
            if (!unit_alive(&u)) return VM_BAD_SCRIPT;
            lhp = rd16(a + 2);
            if (lhp > u.hp) lhp = u.hp;
            vm_duel(a[0], a[1], lhp == 0);              /* platform snapshots the pre-duel sides */
            if (lhp == 0) {
                grid_occ_clear(u.x, u.y);
                u.action_flags |= AF_RETREAT; u.status_mask = 0;
                retreated = 1;
                vm_units_changed = 1;
            }
            u.hp = lhp;
            unit_store(a[1], &u);
            unit_load(a[0], &u);
            u.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);
            unit_store(a[0], &u);
            vm_action_replaced = 1;
            (void)retreated;
            break;
        }
        case OP_FACTION_SET:
            vm_read((uint16_t)(off + 1u), a, 2);
            off += 3;
            if (a[0] >= b->n_units || a[1] > FAC_ENEMY) return VM_BAD_SCRIPT;
            unit_load(a[0], &u);
            if (unit_alive(&u)) {
                grid_occ_clear(u.x, u.y);
                u.faction = a[1];
                u.action_flags |= (uint8_t)(AF_MOVED | AF_ACTED);   /* acts again in its new faction's phase */
                grid_occ_set(u.x, u.y, faction_side(a[1]));
                unit_store(a[0], &u);
                vm_units_changed = 1;
            }
            break;
        default:
            return VM_BAD_SCRIPT;
        }
    }
    return VM_OK;
}

static uint8_t matches(const VmEvent *e, const VmFact *f)
{
    if (e->trigger != f->trigger) return 0;
    if (e->param == VM_ANY) return 1;
    switch (f->trigger) {
    case TR_PHASE_START:
    case TR_PHASE_END: return e->param == f->phase;
    case TR_POST_MOVE:
    case TR_PRE_ACTION:
    case TR_TALK: return e->param == f->actor;
    case TR_UNIT_RETREAT: return e->param == f->unit;
    default: return 1;
    }
}

uint8_t vm_would_fire(const Battle *b, const VmFact *f) CORE_API
{
    uint8_t n = vm_event_count(), i, cnt = 0;
    VmEvent e;
    for (i = 0; i < n; i++) {
        vm_event_header(i, &e);
        if (!matches(&e, f)) continue;
        if (e.once_bit != VM_NO_ONCE && vm_flag_get(SCOPE_BATTLE, e.once_bit)) continue;
        if (cond_true(b, f, e.cond_off)) cnt++;
    }
    return cnt;
}

uint8_t vm_fire(Battle *b, const VmFact *f) CORE_API
{
    uint8_t n = vm_event_count(), i, done_prio = 0, first = 1;
    VmEvent e;
    vm_last_ran = 0; vm_action_replaced = 0; vm_units_changed = 0;
    /* priority ascending, then event index (= id order, compiler-sorted) ascending; no allocation:
     * repeated scans over the small per-battle table */
    for (;;) {
        uint8_t next_prio = 255, any = 0;
        for (i = 0; i < n; i++) {
            vm_event_header(i, &e);
            if (!matches(&e, f)) continue;
            if (!first && e.priority <= done_prio) continue;
            if (e.priority < next_prio) next_prio = e.priority;
            any = 1;
        }
        if (!any) break;
        for (i = 0; i < n; i++) {
            uint8_t r;
            vm_event_header(i, &e);
            if (!matches(&e, f) || e.priority != next_prio) continue;
            if (e.once_bit != VM_NO_ONCE && vm_flag_get(SCOPE_BATTLE, e.once_bit)) continue;
            if (!cond_true(b, f, e.cond_off)) continue;
            {   /* 11.6: one event = one transaction; a failing script leaves no trace */
                static Battle saved;                /* static: vm_fire sits in deep call chains */
                saved = *b;
                undo_begin();
                if (e.once_bit != VM_NO_ONCE) vm_flag_set(SCOPE_BATTLE, e.once_bit, 1);   /* set before running */
                r = run_code(b, f, e.code_off, e.code_len);
                if (r == VM_OK && undo_overflowed()) r = VM_UNDO_CAP;
                vm_last_ran++;
                if (r != VM_OK) { undo_rollback(); *b = saved; return r; }
                undo_end();
            }
            battle_check_outcome(b, 0);                 /* design doc 6.6: check after each atomic step */
            if (b->outcome != OUT_NONE) return VM_OK;   /* terminal: later events do not run */
        }
        done_prio = next_prio; first = 0;
        if (next_prio == 255) break;
    }
    battle_check_outcome(b, 0);
    return VM_OK;
}
