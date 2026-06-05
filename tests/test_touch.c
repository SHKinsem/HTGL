/*
 * test_touch.c — TDD tests for touch/tap input subsystem.
 *
 * Covers:
 *   A) htgl_set_tap_handler stores cb + user.
 *   B) htgl_pointer_down + htgl_pointer_up on same element fires tap callback.
 *   C) Press then release on a different element does NOT fire.
 *   D) Press and release in empty space does NOT fire.
 *   E) Overlapping buttons: higher-indexed (topmost) wins.
 *   F) Non-interactive boxes (font==0) are not hit.
 */

#include <string.h>
#include <stddef.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* ------------------------------------------------------------------ helpers */

#define SW 240
#define SH 320

/*
 * Build a blob with:
 *   node[0] SCREEN 240x320
 *   node[1] BOX font=1 at (10,10,40,40)   <- button A
 *   node[2] BOX font=2 at (60,10,40,40)   <- button B
 *   node[3] BOX font=0 at (10,80,40,40)   <- non-interactive
 *
 * All nodes are direct children of the SCREEN (parent=0).
 * No animations, no strings.
 */
static int build_touch_blob(uint8_t *out, int out_size)
{
    int num_nodes = 4;
    int nodes_end = (int)sizeof(htgl_header) + num_nodes * (int)sizeof(htgl_node);
    /* no anims */
    int strtab_off = nodes_end;

    htgl_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = (uint16_t)num_nodes;
    h.screen_w = SW; h.screen_h = SH;
    h.strtab_off = (uint16_t)strtab_off;
    h.anim_count = 0;

    htgl_node n[4];
    memset(n, 0, sizeof(n));

    /* node 0: SCREEN */
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = SW; n[0].h = SH;

    /* node 1: button A, tap_id=1 */
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].font = 1;
    n[1].x = 10; n[1].y = 10; n[1].w = 40; n[1].h = 40;
    n[1].bg = 0xF800; /* red */

    /* node 2: button B, tap_id=2 */
    n[2].type = HTGL_TYPE_BOX; n[2].parent = 0;
    n[2].font = 2;
    n[2].x = 60; n[2].y = 10; n[2].w = 40; n[2].h = 40;
    n[2].bg = 0x001F; /* blue */

    /* node 3: non-interactive box (font=0) */
    n[3].type = HTGL_TYPE_BOX; n[3].parent = 0;
    n[3].font = 0;
    n[3].x = 10; n[3].y = 80; n[3].w = 40; n[3].h = 40;
    n[3].bg = 0x07E0; /* green */

    (void)out_size;
    memcpy(out,              &h, sizeof(h));
    memcpy(out + sizeof(h), n,  sizeof(n));
    /* empty string table */
    return strtab_off;
}

/*
 * Build a blob with an additional overlapping node C on top of A.
 *   node[0] SCREEN 240x320
 *   node[1] BOX font=1 at (10,10,40,40)   <- button A
 *   node[2] BOX font=2 at (60,10,40,40)   <- button B
 *   node[3] BOX font=0 at (10,80,40,40)   <- non-interactive
 *   node[4] BOX font=3 at (20,15,20,20)   <- button C, overlaps A, drawn LATER (higher index)
 */
static int build_touch_overlap_blob(uint8_t *out, int out_size)
{
    int num_nodes = 5;
    int nodes_end = (int)sizeof(htgl_header) + num_nodes * (int)sizeof(htgl_node);
    int strtab_off = nodes_end;

    htgl_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = (uint16_t)num_nodes;
    h.screen_w = SW; h.screen_h = SH;
    h.strtab_off = (uint16_t)strtab_off;
    h.anim_count = 0;

    htgl_node n[5];
    memset(n, 0, sizeof(n));

    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = SW; n[0].h = SH;

    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].font = 1;
    n[1].x = 10; n[1].y = 10; n[1].w = 40; n[1].h = 40;

    n[2].type = HTGL_TYPE_BOX; n[2].parent = 0;
    n[2].font = 2;
    n[2].x = 60; n[2].y = 10; n[2].w = 40; n[2].h = 40;

    n[3].type = HTGL_TYPE_BOX; n[3].parent = 0;
    n[3].font = 0;
    n[3].x = 10; n[3].y = 80; n[3].w = 40; n[3].h = 40;

    /* C overlaps A in the range (20..39, 15..34), drawn later → topmost */
    n[4].type = HTGL_TYPE_BOX; n[4].parent = 0;
    n[4].font = 3;
    n[4].x = 20; n[4].y = 15; n[4].w = 20; n[4].h = 20;

    (void)out_size;
    memcpy(out,              &h, sizeof(h));
    memcpy(out + sizeof(h), n,  sizeof(n));
    return strtab_off;
}

/* ------------------------------------------------------------------ tap handler */

static int g_last_tap = -99;  /* sentinel: no tap fired yet */
static void record_tap(int tap_id, void *user) {
    (void)user;
    g_last_tap = tap_id;
}

/* Reset between tests */
static void reset_tap(void) { g_last_tap = -99; }
#define NO_TAP_FIRED (-99)

/* Helper: init ctx, load blob, run layout, register handler. */
static int setup_ctx(htgl_ctx *ctx, uint16_t *lb, int lb_px,
                     const uint8_t *blob, int len)
{
    htgl_init(ctx, 0, lb, lb_px);
    if (htgl_load(ctx, blob, len) != 0) return -1;
    htgl_layout(ctx);
    htgl_set_tap_handler(ctx, record_tap, 0);
    return 0;
}

/* ------------------------------------------------------------------ tests */

static int test_tap_button_a(void) {
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    /* down + up inside button A (10..49, 10..49) */
    htgl_pointer_down(&ctx, 20, 20);
    htgl_pointer_up  (&ctx, 20, 20);
    CHECK(g_last_tap == 1);
    return 0;
}

static int test_tap_button_b(void) {
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    /* button B is at (60,10,40,40) → abs (60..99, 10..49) */
    htgl_pointer_down(&ctx, 70, 20);
    htgl_pointer_up  (&ctx, 70, 20);
    CHECK(g_last_tap == 2);
    return 0;
}

static int test_tap_drag_off_no_fire(void) {
    /* Press A, release on B → no tap. */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    htgl_pointer_down(&ctx, 20, 20);   /* inside A */
    htgl_pointer_up  (&ctx, 70, 20);   /* inside B — different node */
    CHECK(g_last_tap == NO_TAP_FIRED);
    return 0;
}

static int test_tap_empty_space_no_fire(void) {
    /* Press in empty space, release → no tap. */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    htgl_pointer_down(&ctx, 200, 200);  /* empty region */
    htgl_pointer_up  (&ctx, 200, 200);
    CHECK(g_last_tap == NO_TAP_FIRED);
    return 0;
}

static int test_tap_non_interactive_no_fire(void) {
    /* Press on the non-interactive box (font==0) → no tap. */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    /* non-interactive box at abs (10,80,40,40) */
    htgl_pointer_down(&ctx, 20, 90);
    htgl_pointer_up  (&ctx, 20, 90);
    CHECK(g_last_tap == NO_TAP_FIRED);
    return 0;
}

static int test_tap_overlap_topmost_wins(void) {
    /* Press in the overlap area of A and C (C drawn later → topmost) → fires 3, not 1. */
    uint8_t blob[512];
    int len = build_touch_overlap_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    /* C covers abs (20..39, 15..34). Point (25, 20) is inside C and also inside A. */
    htgl_pointer_down(&ctx, 25, 20);
    htgl_pointer_up  (&ctx, 25, 20);
    CHECK(g_last_tap == 3);  /* C wins (topmost), not A */
    return 0;
}

static int test_tap_overlap_outside_c_hits_a(void) {
    /* In the overlap blob, tap outside C but still inside A → fires 1. */
    uint8_t blob[512];
    int len = build_touch_overlap_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    /* Point (12, 12) is inside A (10..49,10..49) but NOT inside C (20..39,15..34). */
    htgl_pointer_down(&ctx, 12, 12);
    htgl_pointer_up  (&ctx, 12, 12);
    CHECK(g_last_tap == 1);
    return 0;
}

static int test_tap_no_handler_no_crash(void) {
    /* If no tap handler is set, pointer events must be silent (no crash). */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    htgl_init(&ctx, 0, lb, SW * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    /* No htgl_set_tap_handler call. */
    htgl_pointer_down(&ctx, 20, 20);
    htgl_pointer_up  (&ctx, 20, 20);
    /* If we reach here without crashing, the test passes. */
    return 0;
}

static int test_pressed_node_reset_after_up(void) {
    /* After htgl_pointer_up, pressed_node must be reset to -1. */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);

    htgl_pointer_down(&ctx, 20, 20);
    htgl_pointer_up  (&ctx, 20, 20);
    /* Reset and fire again — second tap must also work */
    reset_tap();
    htgl_pointer_down(&ctx, 20, 20);
    htgl_pointer_up  (&ctx, 20, 20);
    CHECK(g_last_tap == 1);
    return 0;
}

static int test_boundary_pixels(void) {
    /* Test boundary conditions: first and last pixel of button A. */
    uint8_t blob[512];
    int len = build_touch_blob(blob, sizeof(blob));
    htgl_ctx ctx;
    uint16_t lb[SW * 2];

    /* Button A: abs_x=10, abs_y=10, w=40, h=40 → x in [10,49], y in [10,49] */

    /* Top-left corner */
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);
    htgl_pointer_down(&ctx, 10, 10);
    htgl_pointer_up  (&ctx, 10, 10);
    CHECK(g_last_tap == 1);

    /* Bottom-right corner (x=49, y=49 → still inside: abs_x+w-1=49) */
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);
    htgl_pointer_down(&ctx, 49, 49);
    htgl_pointer_up  (&ctx, 49, 49);
    CHECK(g_last_tap == 1);

    /* Just outside right edge (x=50) */
    reset_tap();
    CHECK(setup_ctx(&ctx, lb, SW * 2, blob, len) == 0);
    htgl_pointer_down(&ctx, 50, 20);
    htgl_pointer_up  (&ctx, 50, 20);
    CHECK(g_last_tap == NO_TAP_FIRED);

    return 0;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    int fail = 0;

#define RUN(fn) do { int r = fn(); if (r) { printf("FAILED: " #fn "\n"); fail++; } } while(0)

    RUN(test_tap_button_a);
    RUN(test_tap_button_b);
    RUN(test_tap_drag_off_no_fire);
    RUN(test_tap_empty_space_no_fire);
    RUN(test_tap_non_interactive_no_fire);
    RUN(test_tap_overlap_topmost_wins);
    RUN(test_tap_overlap_outside_c_hits_a);
    RUN(test_tap_no_handler_no_crash);
    RUN(test_pressed_node_reset_after_up);
    RUN(test_boundary_pixels);

    if (fail == 0) printf("ok\n");
    return fail ? 1 : 0;
}
