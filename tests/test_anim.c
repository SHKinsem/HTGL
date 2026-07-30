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
   anim: node_idx=1, prop, mode, from, to, dur_ms.
   Returns total blob length written into `out`. */
static int build_anim_blob(uint8_t *out, int out_size,
                            uint8_t prop, uint8_t mode,
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
    a.node_idx = 1; a.prop = prop; a.mode = mode;
    a.from = from; a.to = to; a.dur_ms = dur_ms;

    (void)out_size;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    memcpy(out + nodes_end, &a, sizeof(a));
    /* empty string table */
    return anims_end;
}

/* Build a V3 blob with a rounded, translucent, blurred glass card. */
static int build_glass_blob(uint8_t *out) {
    int nodes_end = (int)sizeof(htgl_header) + 3 * (int)sizeof(htgl_node);
    int opacity_end = nodes_end + 3;
    int styles_end = opacity_end + 6;

    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 3; h.flags = HTGL_FLAG_OPACITY | HTGL_FLAG_VISUAL_STYLE;
    h.node_count = 3;
    h.screen_w = SCREEN_W; h.screen_h = SCREEN_H;
    h.strtab_off = (uint16_t)styles_end;
    h.anim_count = 0;

    htgl_node n[3];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = SCREEN_W; n[0].h = SCREEN_H; n[0].bg = 0x001F;
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].x = 10; n[1].y = 20; n[1].w = 40; n[1].h = 10; n[1].bg = 0xFFFF;
    n[2].type = HTGL_TYPE_BOX; n[2].parent = 0;
    n[2].x = 10; n[2].y = 10; n[2].w = 40; n[2].h = 40; n[2].bg = 0xF800;
    const uint8_t opacity[3] = {255, 255, 128};
    const uint8_t styles[6] = {0, 0, 0, 0, 8, 2};

    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    memcpy(out + nodes_end, opacity, sizeof(opacity));
    memcpy(out + opacity_end, styles, sizeof(styles));
    return styles_end;
}

/* ------------------------------------------------------------------ Phase A */

static int test_sizeof_anim(void) {
    CHECK(sizeof(htgl_anim) == 10);
    /* field offsets */
    CHECK(offsetof(htgl_anim, node_idx) == 0);
    CHECK(offsetof(htgl_anim, prop)     == 2);
    CHECK(offsetof(htgl_anim, mode)     == 3);
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
    CHECK(ctx.anims[0].mode     == 0);  /* once */
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

/* Geometry is kept in Q24.8 even though raster output is integer pixels.
   A one-pixel move therefore retains quarter-pixel progress, and the caller
   is told to redraw only once rounding reaches a visible pixel. */
static int test_tick_geometry_q24_8(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /* x */, 0 /* once */, 0, 1, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    /* Quarter of a 1px move: retained in fixed-point, not visible yet. */
    CHECK(htgl_tick(&ctx, 250) == 0);
    CHECK(ctx.cur_x_fp[1] == 64);   /* 0.25 * 256 */
    CHECK(ctx.cur_x[1] == 0);

    /* Halfway rounds to the next raster pixel. */
    CHECK(htgl_tick(&ctx, 500) == 1);
    CHECK(ctx.cur_x_fp[1] == 128);  /* 0.5 * 256 */
    CHECK(ctx.cur_x[1] == 1);
    return 0;
}

/* ------------------------------------------------------------------ Phase C */

/* Capture HAL for render tests. */
#define RW 240
#define RH 200
static uint16_t g_fb[RW * RH];
static int g_flush_count;
static int g_last_flush_x, g_last_flush_y, g_last_flush_w, g_last_flush_h;

static void cap_flush(int x, int y, int w, int h, const uint16_t *buf) {
    g_flush_count++;
    g_last_flush_x = x; g_last_flush_y = y;
    g_last_flush_w = w; g_last_flush_h = h;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            g_fb[(y + row) * RW + (x + col)] = buf[row * w + col];
}

static int test_render_rect_composites_only_requested_region(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /* x */, 0 /* once */, 30, 100, 1000);
    htgl_ctx ctx;
    uint16_t lb[RW * 4];
    htgl_hal hal = { cap_flush };
    memset(g_fb, 0xA5, sizeof(g_fb));
    htgl_init(&ctx, &hal, lb, RW * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);

    g_flush_count = 0;
    htgl_render_rect(&ctx, 32, 0, 5, 30);
    CHECK(g_flush_count == 1);
    CHECK(g_last_flush_x == 32 && g_last_flush_y == 0);
    CHECK(g_last_flush_w == 5 && g_last_flush_h == 30);
    CHECK(g_fb[0 * RW + 32] == 0xF800);  /* red box replayed in dirty region */
    CHECK(g_fb[0 * RW + 31] == 0xA5A5);  /* neighbour was never flushed */

    g_flush_count = 0;
    htgl_render_rect(&ctx, -5, -5, 2, 2);
    CHECK(g_flush_count == 0);            /* wholly off-screen is a no-op */
    return 0;
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

static int test_v3_rounded_glass_render(void) {
    uint8_t blob[256];
    int len = build_glass_blob(blob);
    htgl_ctx ctx;
    uint16_t lb[RW * 4];
    htgl_hal hal = { cap_flush };
    memset(g_fb, 0, sizeof(g_fb));
    htgl_init(&ctx, &hal, lb, RW * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(ctx.styles != 0);
    CHECK(ctx.styles[4] == 8 && ctx.styles[5] == 2);
    CHECK(ctx.cur_opacity[2] == 128);
    htgl_layout(&ctx);
    htgl_render(&ctx);
    CHECK(g_fb[10 * RW + 10] == 0x001F);  /* rounded corner preserves backdrop */
    CHECK(g_fb[10 * RW + 30] != 0x001F);  /* top edge is inside the rounded card */
    CHECK(g_fb[20 * RW + 30] != 0xFFFF);  /* glass tint changes the white stripe */
    return 0;
}

static int test_tick_opacity_animation(void) {
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 5 /* opacity */, 0, 0, 255, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(htgl_tick(&ctx, 0) == 1);
    CHECK(ctx.cur_opacity[1] == 0);
    CHECK(htgl_tick(&ctx, 500) == 1);
    CHECK(ctx.cur_opacity[1] == 127);
    CHECK(htgl_tick(&ctx, 1000) == 1);
    CHECK(ctx.cur_opacity[1] == 255);
    return 0;
}

/* ------------------------------------------------------------------ Phase D: easing */

/*
 * Easing math (mode byte = loop | (ease<<4)):
 *   p = num*256/den   (num/den = 500/1000 = 0.5 → p=128)
 *
 *   ease=0 (linear):      p unchanged (128) → value = 0 + 200*128/256 = 100
 *   ease=1 (ease-in):     p = p*p/256 = 128*128/256 = 64 → 0+200*64/256 = 50
 *   ease=2 (ease-out):    q=256-128=128; p=256-128*128/256=256-64=192 → 200*192/256=150
 *   ease=3 (ease-in-out): p<128: p=p*p/128=128*128/128=128 → 200*128/256=100
 *     (at exact halfway, ease-in-out equals linear)
 *
 * For once, from=0, to=200, dur=1000, now=500.
 */

static int test_tick_easing_linear(void) {
    /* ease=0 (linear): mode = 0 (once) | (0<<4) = 0 */
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0 /*mode=once+linear*/, 0, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);
    CHECK(ctx.cur_x[1] == 100);
    return 0;
}

static int test_tick_easing_ease_in(void) {
    /* ease=1 (ease-in): mode = 0 (once) | (1<<4) = 0x10 */
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0x10 /*mode=once+ease-in*/, 0, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);
    /* p=128 -> p*p/256=64 -> 0+200*64/256=50 */
    CHECK(ctx.cur_x[1] == 50);
    return 0;
}

static int test_tick_easing_ease_out(void) {
    /* ease=2 (ease-out): mode = 0 (once) | (2<<4) = 0x20 */
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0 /*x*/, 0x20 /*mode=once+ease-out*/, 0, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);
    /* p=128 -> q=128 -> 256-128*128/256=256-64=192 -> 200*192/256=150 */
    CHECK(ctx.cur_x[1] == 150);
    return 0;
}

static int test_tick_easing_endpoints_unchanged(void) {
    /* At t=0 and t=dur, all easing curves must produce from and to exactly. */
    uint8_t blob[256];
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];

    /* ease-in at t=0 */
    int len = build_anim_blob(blob, sizeof(blob), 0, 0x10 /*ease-in once*/, 0, 200, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 0);
    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 200);

    /* ease-out at t=0 and t=dur */
    len = build_anim_blob(blob, sizeof(blob), 0, 0x20 /*ease-out once*/, 0, 200, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 0);
    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 200);

    /* ease-in-out at t=0 and t=dur */
    len = build_anim_blob(blob, sizeof(blob), 0, 0x30 /*ease-in-out once*/, 0, 200, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_x[1] == 0);
    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 200);

    return 0;
}

static int test_tick_easing_loop_mode_preserved(void) {
    /* ease-in with loop mode (mode = 1 | (1<<4) = 0x11) */
    uint8_t blob[256];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0x11 /*mode=loop+ease-in*/, 0, 200, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    /* t=1000 in loop mode wraps → num=0, p=0, value=0 */
    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_x[1] == 0);
    return 0;
}

/* ------------------------------------------------------------------ Phase E: bg color animation */

/*
 * bg anim: prop=4, from=0xF800 (red), to=0x001F (blue), dur=1000, once (loop=0).
 * RGB565 channels:
 *   red   0xF800 = r:31 g:0  b:0
 *   blue  0x001F = r:0  g:0  b:31
 *
 * At t=0:   p=0   -> cur_bg = from = 0xF800
 * At t=1000: p=256 -> cur_bg = to   = 0x001F
 * At t=500:  p=128 -> r=31+(0-31)*128/256=31-15=16,  g=0, b=0+(31-0)*128/256=15
 *   -> (16<<11)|(0<<5)|15 = 0x800F
 *
 * Wait — let's recompute carefully:
 *   r = fr + (tr-fr)*p/256 = 31 + (0-31)*128/256 = 31 - 31*128/256 = 31 - 15 = 16
 *   g = 0
 *   b = 0  + (31-0)*128/256 = 31*128/256 = 15
 *   result = (16<<11)|(0<<5)|15 = 0x800F
 *
 * Actually the spec says 0x780F. Let me check:
 *   spec says r:31→0 ⇒ 15.  But: fr=31, tr=0, p=128
 *   r = 31 + (0-31)*128/256 = 31 - 3968/256 = 31 - 15 = 16  (integer division: 31*128=3968, 3968/256=15)
 *   So r=16, not 15. (15<<11=0x7800, 16<<11=0x8000)
 *   spec says (15<<11)|(0<<5)|15 = 0x780F — that would require r=15.
 *   But integer: (tr-fr)*p/256 = (0-31)*128/256 = -3968/256 = -15 (C integer div rounds toward zero)
 *   r = 31 + (-15) = 16.  So spec comment was slightly off; correct value is 0x800F.
 */

/* Build a blob with prop=4 (bg), signed-wrapping the RGB565 color as int16. */
static int build_bg_anim_blob(uint8_t *out,
                               uint16_t from_rgb565, uint16_t to_rgb565,
                               uint8_t mode, uint16_t dur_ms)
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
    n[1].x = 10; n[1].y = 10; n[1].w = 50; n[1].h = 50;
    n[1].bg = from_rgb565;  /* initial static color */

    /* signed-wrap the RGB565 values into int16 (bit-preserving) */
    int16_t from16 = (int16_t)from_rgb565;
    int16_t to16   = (int16_t)to_rgb565;

    htgl_anim a;
    a.node_idx = 1; a.prop = 4 /*bg*/; a.mode = mode;
    a.from = from16; a.to = to16; a.dur_ms = dur_ms;

    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    memcpy(out + nodes_end, &a, sizeof(a));
    return anims_end;
}

static int test_bg_anim_tick_start(void) {
    /* t=0 -> cur_bg must equal from (0xF800, red) */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0xF800, 0x001F, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 0);
    CHECK(ctx.cur_bg[1] == 0xF800);
    return 0;
}

static int test_bg_anim_tick_end(void) {
    /* t=1000 (end) -> cur_bg must equal to (0x001F, blue) */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0xF800, 0x001F, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 1000);
    CHECK(ctx.cur_bg[1] == 0x001F);
    return 0;
}

static int test_bg_anim_tick_midpoint(void) {
    /*
     * t=500, p=128:
     *   from=0xF800: r=31, g=0,  b=0
     *   to  =0x001F: r=0,  g=0,  b=31
     *   r = 31 + (0-31)*128/256  = 31 - 3968/256 = 31 - 15 = 16
     *   g = 0
     *   b = 0  + 31*128/256      = 3968/256 = 15
     *   result = (16<<11)|(0<<5)|15 = 0x800F
     */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0xF800, 0x001F, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);
    uint16_t expected = (uint16_t)((16 << 11) | (0 << 5) | 15);  /* 0x800F */
    CHECK(ctx.cur_bg[1] == expected);
    return 0;
}

static int test_bg_anim_init_from_node(void) {
    /* After htgl_load (before any tick), cur_bg[i] == nodes[i].bg */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0xF800, 0x001F, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(ctx.cur_bg[1] == 0xF800);
    return 0;
}

static int test_bg_anim_render_uses_cur_bg(void) {
    /*
     * Build a blob with prop=4 bg, from=0x0000(black) to=0xFFFF(white-ish), once 1000ms.
     * Box is at x=10,y=10,w=50,h=50.
     * At t=0: box pixels should be 0x0000 (black).
     * At t=1000: box pixels should match cur_bg after tick.
     * Verify render uses cur_bg not n->bg.
     */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0x0000 /*black*/, 0xFFFF /*white*/, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[RW * 4];
    htgl_hal hal = { cap_flush };
    memset(g_fb, 0xFF, sizeof(g_fb));  /* fill with non-zero to detect background */
    htgl_init(&ctx, &hal, lb, RW * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    /* At t=0: box should render as black (0x0000) */
    htgl_tick(&ctx, 0);
    htgl_layout(&ctx);
    memset(g_fb, 0, sizeof(g_fb));
    htgl_render(&ctx);
    /* Box covers pixel (10,10) */
    CHECK(g_fb[10 * RW + 10] == 0x0000);

    /* At t=1000: box animates to white (0xFFFF) */
    htgl_tick(&ctx, 1000);
    htgl_layout(&ctx);
    memset(g_fb, 0, sizeof(g_fb));
    htgl_render(&ctx);
    CHECK(ctx.cur_bg[1] == 0xFFFF);
    CHECK(g_fb[10 * RW + 10] == 0xFFFF);

    return 0;
}

static int test_bg_anim_does_not_touch_cur_xy(void) {
    /* prop=4 (bg) must NOT modify cur_x/cur_y/cur_w/cur_h */
    uint8_t blob[256];
    int len = build_bg_anim_blob(blob, 0xF800, 0x001F, 0 /*once*/, 1000);
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);

    int16_t x_before = ctx.cur_x[1];
    int16_t y_before = ctx.cur_y[1];
    int16_t w_before = ctx.cur_w[1];
    int16_t h_before = ctx.cur_h[1];

    htgl_tick(&ctx, 500);

    CHECK(ctx.cur_x[1] == x_before);
    CHECK(ctx.cur_y[1] == y_before);
    CHECK(ctx.cur_w[1] == w_before);
    CHECK(ctx.cur_h[1] == h_before);
    return 0;
}

/* --------------------------------------------------- Phase F: branch coverage */

static int test_tick_easing_ease_in_out_interior(void) {
    /* ease-in-out has two sub-branches meeting at p=128; existing tests only hit
       p=128 (t=500). Exercise p<128 (t=250) and p>=128 (t=750). once, 0..200. */
    uint8_t blob[256];
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0x30 /*ease-in-out once*/, 0, 200, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 250);                       /* p=64<128: p*p/128=32 -> 200*32/256=25 */
    CHECK(ctx.cur_x[1] == 25);
    htgl_tick(&ctx, 750);                       /* p=192>=128: 256-64*64/128=224 -> 175 */
    CHECK(ctx.cur_x[1] == 175);
    return 0;
}

static int test_tick_prop_y_and_h(void) {
    /* prop=1 (y) and prop=3 (h) drive cur_y / cur_h (other tests only used x/w). */
    uint8_t blob[256];
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];

    int len = build_anim_blob(blob, sizeof(blob), 1 /*y*/, 0, 0, 100, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);                       /* linear p=128 -> 0+100*128/256=50 */
    CHECK(ctx.cur_y[1] == 50);

    len = build_anim_blob(blob, sizeof(blob), 3 /*h*/, 0, 100, 200, 1000);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_tick(&ctx, 500);                       /* 100+(200-100)*128/256=150 */
    CHECK(ctx.cur_h[1] == 150);
    return 0;
}

static int test_tick_dur_zero_no_crash(void) {
    /* dur_ms==0 is blob-controlled; the guard must skip it (no divide-by-zero). */
    uint8_t blob[256];
    htgl_ctx ctx;
    uint16_t lb[SCREEN_W * 2];
    int len = build_anim_blob(blob, sizeof(blob), 0, 0, 10, 200, 0 /*dur=0*/);
    htgl_init(&ctx, 0, lb, SCREEN_W * 2);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(htgl_tick(&ctx, 500) == 0);           /* skipped: no change, no SIGFPE */
    CHECK(ctx.cur_x[1] == 10);                  /* stays at init (n.x = from) */
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
    RUN(test_tick_geometry_q24_8);

    /* Phase C */
    RUN(test_layout_uses_cur_x);
    RUN(test_render_uses_cur_w);
    RUN(test_render_rect_composites_only_requested_region);
    RUN(test_v3_rounded_glass_render);
    RUN(test_tick_opacity_animation);

    /* Phase D — easing */
    RUN(test_tick_easing_linear);
    RUN(test_tick_easing_ease_in);
    RUN(test_tick_easing_ease_out);
    RUN(test_tick_easing_endpoints_unchanged);
    RUN(test_tick_easing_loop_mode_preserved);

    /* Phase E — bg color animation */
    RUN(test_bg_anim_init_from_node);
    RUN(test_bg_anim_tick_start);
    RUN(test_bg_anim_tick_end);
    RUN(test_bg_anim_tick_midpoint);
    RUN(test_bg_anim_render_uses_cur_bg);
    RUN(test_bg_anim_does_not_touch_cur_xy);

    /* Phase F — branch coverage */
    RUN(test_tick_easing_ease_in_out_interior);
    RUN(test_tick_prop_y_and_h);
    RUN(test_tick_dur_zero_no_crash);

    if (fail == 0) printf("ok\n");
    return fail ? 1 : 0;
}
