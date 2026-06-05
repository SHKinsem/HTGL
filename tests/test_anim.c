/*
 * test_anim.c — TDD tests for the animation runtime (phases A and B).
 *
 * Covers:
 *   A) Load with anim table: pointer/count correct, bad node_idx rejected,
 *      sizeof(htgl_anim)==10, strtab_off validation.
 *   B) htgl_tick interpolation for once/loop/pingpong.
 *   C) Layout reflects cur_x after tick; render reflects cur_w after tick.
 */

#include <string.h>
#include <stddef.h>   /* offsetof */
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* ------------------------------------------------------------------ helpers */

#define SCREEN_W 240
#define SCREEN_H 200

/* Build a blob with SCREEN + 1 BOX and one anim record.
   anim: node_idx=1, prop, loop, from, to, dur_ms.
   Returns total blob length written into `out`. */
static int build_anim_blob(uint8_t *out, int out_size,
                            uint8_t prop, uint8_t loop,
                            int16_t from, int16_t to, uint16_t dur_ms)
{
    int nodes_end = (int)sizeof(htgl_header) + 2 * (int)sizeof(htgl_node);
    int anims_end = nodes_end + (int)sizeof(htgl_anim);

    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 2;
    h.screen_w = SCREEN_W; h.screen_h = SCREEN_H;
    h.strtab_off = (uint16_t)anims_end;
    h.anim_count = 1;

    htgl_node n[2];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = SCREEN_W; n[0].h = SCREEN_H;
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].x = from; n[1].y = 0; n[1].w = 30; n[1].h = 30; n[1].bg = 0xF800;

    htgl_anim a;
    a.node_idx = 1; a.prop = prop; a.loop = loop;
    a.from = from; a.to = to; a.dur_ms = dur_ms;

    (void)out_size;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    memcpy(out + nodes_end, &a, sizeof(a));
    /* empty string table */
    return anims_end;
}

/* ------------------------------------------------------------------ Phase A */

static int test_sizeof_anim(void) {
    CHECK(sizeof(htgl_anim) == 10);
    /* field offsets */
    CHECK(offsetof(htgl_anim, node_idx) == 0);
    CHECK(offsetof(htgl_anim, prop)     == 2);
    CHECK(offsetof(htgl_anim, loop)     == 3);
    CHECK(offsetof(htgl_anim, from)     == 4);
    CHECK(offsetof(htgl_anim, to)       == 6);
    CHECK(offsetof(htgl_anim, dur_ms)   == 8);
    return 0;
}

static int test_load_with_anim(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0, 10, 200, 1000);

    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(ctx.anim_count == 1);
    CHECK(ctx.anims != 0);
    CHECK(ctx.anims[0].node_idx == 1);
    CHECK(ctx.anims[0].prop     == 0);  /* x */
    CHECK(ctx.anims[0].loop     == 0);  /* once */
    CHECK(ctx.anims[0].from     == 10);
    CHECK(ctx.anims[0].to       == 200);
    CHECK(ctx.anims[0].dur_ms   == 1000);

    /* cur_* initialised from static node fields */
    CHECK(ctx.cur_x[1] == 10);  /* node[1].x = from = 10 */
    CHECK(ctx.cur_w[1] == 30);  /* node[1].w */
    CHECK(ctx.cur_h[1] == 30);  /* node[1].h */
    return 0;
}

static int test_bad_node_idx_rejected(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0, 10, 200, 1000);

    /* corrupt: node_idx = 99 (>= node_count=2) */
    htgl_anim *a = (htgl_anim *)(blob + sizeof(htgl_header) + 2 * sizeof(htgl_node));
    a->node_idx = 99;

    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) != 0);
    return 0;
}

static int test_anim_table_past_strtab_rejected(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0, 10, 200, 1000);

    /* corrupt: strtab_off too small — it lands BEFORE the anim table ends */
    htgl_header *h = (htgl_header *)blob;
    h->strtab_off = (uint16_t)(sizeof(htgl_header) + 2 * sizeof(htgl_node));  /* no room for anim */

    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) != 0);
    return 0;
}

static int test_no_anim_blob_still_loads(void) {
    /* A blob with anim_count=0 (old format) must still load cleanly. */
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 1;
    h.screen_w = 100; h.screen_h = 100;
    h.strtab_off = sizeof(htgl_header) + sizeof(htgl_node);
    h.anim_count = 0;
    htgl_node n;
    memset(&n, 0, sizeof(n));
    n.type = HTGL_TYPE_SCREEN; n.parent = HTGL_ROOT_PARENT;
    n.w = 100; n.h = 100;

    uint8_t blob[128];
    memcpy(blob, &h, sizeof(h));
    memcpy(blob + sizeof(h), &n, sizeof(n));
    int len = (int)(sizeof(h) + sizeof(n));

    htgl_ctx ctx;
    uint16_t lb[100];
    htgl_init(&ctx, 0, lb, 100);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(ctx.anim_count == 0);
    return 0;
}

/* ------------------------------------------------------------------ Phase B */

/*
 * Exact integer expectations:
 *   value = from + (to-from)*num/den  where den = dur_ms.
 * For from=10, to=200, dur=1000:
 *   once   t=0   -> num=0,   val=10 + 190*0/1000   = 10
 *   once   t=500 -> num=500, val=10 + 190*500/1000 = 10+95 = 105
 *   once   t=1000-> num=1000,val=10 + 190*1000/1000= 200
 *   once   t=5000-> num=1000 (clamped), val=200
 *
 *   loop   t=0   -> num=0,   val=10
 *   loop   t=500 -> num=500, val=105
 *   loop   t=1000-> num=0 (wrapped), val=10
 *
 *   pingpong t=0   -> c2=0,    num=0,   val=10
 *   pingpong t=500 -> c2=500,  num=500, val=105
 *   pingpong t=1000-> c2=1000, num=1000,val=200
 *   pingpong t=1500-> c2=1500, num=2000-1500=500, val=105
 *   pingpong t=2000-> c2=0,    num=0,   val=10
 */

static int test_tick_once(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0 /*once*/, 10, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 10);

    htgl_tick(&ctx, 500);
    CHECK(ctx.cur_x[1] == 105);

    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 200);

    htgl_tick(&ctx, 5000);
    CHECK(ctx.cur_x[1] == 200);   /* clamped */
    return 0;
}

static int test_tick_loop(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 1 /*loop*/, 10, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 10);

    htgl_tick(&ctx, 500);
    CHECK(ctx.cur_x[1] == 105);

    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 10);   /* wrapped back */
    return 0;
}

static int test_tick_pingpong(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 2 /*pingpong*/, 10, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 10);

    htgl_tick(&ctx, 500);
    CHECK(ctx.cur_x[1] == 105);

    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 200);

    htgl_tick(&ctx, 1500);
    CHECK(ctx.cur_x[1] == 105);

    htgl_tick(&ctx, 2000);
    CHECK(ctx.cur_x[1] == 10);
    return 0;
}

/* htgl_tick returns 1 when value changes, 0 when it doesn't. */
static int test_tick_return_value(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0 /*once*/, 10, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    /* t=0: cur_x[1] was already 10 (init from static), should be 0 (no change) */
    CHECK(htgl_tick(&ctx, 0) == 0);
    /* t=500: value changes (10 + 190*500/1000 = 105 != 10) */
    CHECK(htgl_tick(&ctx, 500) == 1);
    /* t=500 again: same value, no change */
    CHECK(htgl_tick(&ctx, 500) == 0);
    /* t=1000 (clamped end): changes to 200 */
    CHECK(htgl_tick(&ctx, 1000) == 1);
    /* t=5000: still 200, no change */
    CHECK(htgl_tick(&ctx, 5000) == 0);
    return 0;
}

/* ------------------------------------------------------------------ Phase C */

/* Capture HAL for render tests. */
#define RW 240
#define RH 200
static uint16_t g_fb[RW * RH];

static void cap_flush(int x, int y, int w, int h, const uint16_t *buf) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            g_fb[(y + row) * RW + (x + col)] = buf[row * w + col];
}

static int test_layout_uses_cur_x(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0 /*once*/, 10, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[RW * 4];
    htgl_hal hal = { cap_flush };
    htgl_init(&ctx, &hal, lb, RW * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    /* Without tick: static x=10 */
    htgl_layout(&ctx);
    CHECK(ctx.abs_x[1] == 10);

    /* After tick to mid: x=105 */
    htgl_tick(&ctx, 500);
    htgl_layout(&ctx);
    CHECK(ctx.abs_x[1] == 105);
    return 0;
}

static int test_render_uses_cur_w(void) {
    /* Build a blob animating 'w' (prop=2): static w=30, anim from=30 to=100 dur=1000 once. */
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 2 /*w*/, 0 /*once*/, 30, 100, 1000);
    htgl_ctx ctx;
    uint16_t lb[RW * 4];
    htgl_hal hal = { cap_flush };
    memset(g_fb, 0, sizeof(g_fb));
    htgl_init(&ctx, &hal, lb, RW * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    /* At t=0: width should still be 30. Render and check pixel just past w=30 is bg. */
    htgl_tick(&ctx, 0);
    htgl_layout(&ctx);
    htgl_render(&ctx);
    /* node[1].x=30, y=0, at static tick=0 w=30 -> pixels [30,59] are red, [60] is bg */
    CHECK(g_fb[0 * RW + 35] == 0xF800);    /* inside box */
    CHECK(g_fb[0 * RW + 60] != 0xF800);    /* outside box */

    /* At t=1000: w should animate to 100. */
    htgl_tick(&ctx, 1000);
    htgl_layout(&ctx);
    memset(g_fb, 0, sizeof(g_fb));
    htgl_render(&ctx);
    CHECK(g_fb[0 * RW + 35] == 0xF800);    /* still inside */
    CHECK(g_fb[0 * RW + 95] == 0xF800);    /* now inside (w expanded to 100) */
    CHECK(g_fb[0 * RW + 130] != 0xF800);   /* still outside */
    return 0;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    int fail = 0;

#define RUN(fn) do { int r = fn(); if (r) { printf("FAILED: " #fn "\n"); fail++; } } while(0)

    /* Phase A */
    RUN(test_sizeof_anim);
    RUN(test_load_with_anim);
    RUN(test_bad_node_idx_rejected);
    RUN(test_anim_table_past_strtab_rejected);
    RUN(test_no_anim_blob_still_loads);

    /* Phase B */
    RUN(test_tick_once);
    RUN(test_tick_loop);
    RUN(test_tick_pingpong);
    RUN(test_tick_return_value);

    /* Phase C */
    RUN(test_layout_uses_cur_x);
    RUN(test_render_uses_cur_w);

    if (fail == 0) printf("ok\n");
    return fail ? 1 : 0;
}
